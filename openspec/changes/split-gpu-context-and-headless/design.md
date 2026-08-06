## Context

The engine's rendering pipeline currently requires a `SDL_Window`, Vulkan surface, and `Swapchain` to function. `RenderSystem` is a god object owning 14+ subsystems. GPU physics (`XpbdGpuSolver`, `SpatialHashBroadDetector`, etc.) and compute operations cannot run without the full rendering stack. `AllocatorState` depends on `RenderSystem&` despite only needing `vk::Device`, `vk::PhysicalDevice`, and `vk::Instance` — all available from `DeviceInterface`.

The project already has a precedent for standalone DLLs: `Core` is a shared library exporting foundational types. `GpuContext` follows the same pattern for Vulkan device management and memory allocation.

## Goals / Non-Goals

**Goals:**
- Extract `DeviceInterface`, `AllocatorState`, `MemoryTypes`, `MemoryAllocation` into a standalone `GpuContext` DLL with no dependency on the Render module
- Enable true headless GPU operation: no `SDL_Window*`, no `vk::SurfaceKHR`, no `vk::SwapchainKHR`, no `vkQueuePresentKHR`
- Provide a clean `IPresentProvider` interface so `FrameManager` works identically with or without a swapchain
- Support offscreen rendering with CPU readback in headless mode
- Verify the architecture with two tests: pure compute shader (GpuContext only) and offscreen rendering (engine.dll headless)

**Non-Goals:**
- Refactoring `MainClass::Initialize` beyond adding a headless path
- Modifying physics system code (solvers, detectors, `PhysicsScene`)
- True headless rendering via `VK_EXT_headless_surface` (still uses standard Vulkan without surface extensions)
- RHI (Render Hardware Interface) abstraction — `GpuContext` exposes raw `vk::` types
- `AllocatorState` image allocation API changes (stays as-is, just depends on `DeviceInterface&`)

## Decisions

### Design at a glance

The presentation layer performs exactly **one frame-completion submit per frame**. The main render CB and an optional copy CB (blit final RTT → swapchain image) are submitted as a single `vkQueueSubmit2` batch — the two CBs execute in order inside the batch, so no synchronization object exists between them; barriers handle layout transitions. All frame-lifecycle synchronization (`timeline_semaphores[3]` with timepoints 1/2/4, `image_acquired_semaphores[3]`, `command_executed_fences[3]`, main CB lifecycle) is owned by `FrameManager`. The provider interface exposes only four operations:

```cpp
uint32_t AcquireNextImage(vk::Device, vk::Semaphore image_ready_semaphore, uint64_t timeout);  // async; MUST signal
vk::CommandBuffer PrepareCopy(vk::Device, const RenderTargetTexture&, uint32_t, MemoryAccessTypeImageBits);  // or nullptr
bool Present(vk::Device, uint32_t image_index, vk::Semaphore frame_done_semaphore);
void Recreate(vk::Extent2D);
```

The only synchronization object crossing the interface is the **frame completion credential** — a binary semaphore (`frame_completed_semaphores[fif]`) signaled by the frame-completion batch, passed into `Present`, meaning "present must wait until this frame (including the copy) is complete". Note the credential is a binary semaphore, not the `timeline@4` value: `vkQueuePresentKHR` accepts only binary semaphores in its wait list. `RenderGraph` records passes only; submission happens at the frame-completion point (`RenderSystem::CompleteFrame` → `FrameManager::SubmitFrame`). The decisions below record the reasoning; names from earlier iterations (e.g. `FrameSyncInfo`, `PresentToFramebuffer`) are historical and do not exist in the final design.

### Decision 1: `IPresentProvider` Strategy Pattern

**Chosen**: `IPresentProvider` interface with `SwapchainPresentProvider` and `HeadlessPresentProvider` implementations.

**Rationale**: Without this, `FrameManager::SubmitFrame`, `RenderSystem::CompleteFrame`, `CommandBuffer::DrawRenderers`, and `ComplexRenderGraphBuilder` would each need `if (headless)` branches scattered across 5+ files. The Strategy pattern localizes the variation into two implementations of a single interface.

**Alternatives considered**:
- *If-checks everywhere*: Rejected — leads to brittle, hard-to-test code with hidden coupling.
- *Nullable swapchain + headless-swapchain stub*: Requires `Swapchain` class to handle both modes internally. Rejected — mixes orthogonal concerns; headless "swapchain" has no images, no present, no barrier to define.

**Interface location**: `engine/Render/RenderSystem/IPresentProvider.h` — allows `RenderTargetTexture` in `CompleteFrame` signature without cross-module dependency, since `RenderSystem` state classes already have access to Render types.

### Decision 1b: Frame completion credential — the interface's only synchronization object

**Chosen**: The provider interface carries NO sync-object arrays. The only synchronization object crossing the interface is the **frame completion credential** — a binary semaphore (`frame_completed_semaphores[fif]`) signaled by the frame-completion batch and passed into `Present`, expressing "present must wait until the frame — including the copy — has completed". (Binary, because `vkQueuePresentKHR` accepts only binary semaphores in its wait list.)

```cpp
// IPresentProvider (final)
uint32_t AcquireNextImage(vk::Device, vk::Semaphore image_ready_semaphore, uint64_t timeout);
vk::CommandBuffer PrepareCopy(vk::Device, const RenderTargetTexture&, uint32_t image_index,
                              MemoryAccessTypeImageBits last_access);
bool Present(vk::Device, uint32_t image_index, vk::Semaphore frame_done_semaphore);
```

**Rationale**:
- FrameManager executes the frame-completion submit itself (it owns all sync primitives); the provider only *records* the copy CB (`PrepareCopy`) and *presents* (`Present`). No "signal on the provider's behalf" seam exists.
- A per-image `copy_completed` semaphore set (as in the earlier array-passing iteration) is over-engineering: once the copy is merged into the frame-completion batch, "copy complete" is not a distinct event — it is part of "frame complete" (`timeline@4`). Present always shows the current frame's image, so waiting on the current frame's credential is exact.
- The credential direction is explicit: FrameManager lends its timeline; the provider consumes it. No implicit "must equal signal[0]" constraints.

**Rejected alternatives**:
- *Pre-built wait/signal arrays + fence passed into a provider `CompleteFrame` method (an early iteration named this struct `FrameSyncInfo`)*: forced the provider to submit with FrameManager's sync objects; the headless no-op adapter could legally skip that submit and break the whole sync chain (see Decision 1d).
- *FrameManager querying a provider-owned per-image semaphore (an early iteration's `GetCopyCompletedSemaphore()`)*: FrameManager signaling a provider-owned semaphore inverts ownership and re-introduces a per-image semaphore set for no benefit.
- *Synchronous acquire (internal fence + `waitForFences` in `AcquireNextImage`)*: blocks the CPU; breaks the multi-frame-in-flight pipeline. Rejected in favor of the async binary-semaphore contract (Decision 8).

### Decision 1c: `SwapchainPresentProvider` owns copy command buffers (recording only)

**Chosen**: `PrepareCopy` records the blit (RTT → swapchain image) into one of the provider's own command buffers (one per swapchain image, allocated from the graphics command pool) and returns it. FrameManager submits it as part of the frame-completion batch. Headless returns `nullptr`.

**Rationale**: The copy buffers exist only to blit the final RTT into a swapchain image — a pure swapchain concern. Recording stays in the provider (it owns the swapchain image handles and layout knowledge, and derives the source-layout barrier from `last_access` via `GetImageLayout`); submission stays in FrameManager (it owns all synchronization).

**Reuse safety**: a swapchain image is only re-acquired after its previous present completes (the present waits on the frame completion credential, which fires after the copy batch), so the previous copy has finished before `PrepareCopy` resets the buffer for that image index.

### Decision 1d: `SubmitFrame` — the single frame-completion submit (merges the former `SubmitMainCommandBuffer` + `PresentToFramebuffer`)

**Chosen**: `FrameManager::SubmitFrame(present_texture, last_access)` is the single frame-completion point. It: ends the main CB → records the copy CB via `PrepareCopy` → submits ONE `vkQueueSubmit2` batch containing both the main render CB and the copy CB (in-order execution, zero signals between them; barriers handle layout) → presents → runs `impl::CompleteFrame()` (readback, FIF advance, timeline `EndFrame()`).

**Rationale**:
- The main CB's lifecycle (begin/record/end/submit) belongs to FrameManager, but its recording is driven by the render layer. The submit must happen after the present target is known — and the present target (final RTT) is only known at frame-completion time. Therefore the submit point and the present point are necessarily the same moment: they merge into `SubmitFrame`. The former separate methods `SubmitMainCommandBuffer` (submit main CB) and `PresentToFramebuffer` (build sync + present) merge into it.
- Batch-internal ordering makes copy-vs-render synchronization free: `{main CB, copy CB}` execute in order; the copy CB's barriers handle the layout transition (`GetImageLayout({last_access})` for the source).

**History**: an earlier iteration delegated blit+submit+present to a provider method `CompleteFrame` (from an earlier interface iteration). The headless no-op implementation skipped the only submit that signals `timeline@4` + the command-executed fence → GPU queue stall from frame 2, CPU hang from frame 4 (`waitForFences` with `UINT64_MAX`). Lesson: the sync-chain invariant ("exactly one frame-completion submit per frame") must live in FrameManager, not in adapter behavior. `SubmitFrame` guarantees it structurally — the headless batch simply carries no copy CB.

### Decision 1e: Synchronization ownership boundary

**Chosen**:

| Owned by FrameManager | Owned by IPresentProvider |
|---|---|
| `timeline_semaphores[3]` (timepoints 1/2/4: start / staged upload / frame complete) | `VkSwapchainKHR` + `swapchain_images[]` |
| `image_acquired_semaphores[3]` (async acquire → batch gate) | extent, color format, image count |
| `command_executed_fences[3]` (CPU-GPU throttle) | copy command buffers (one per image, recording only) |
| main command buffers (begin/end/submit) | acquire completion notification (MUST signal `image_ready`) |
| frame-completion submit (main CB + copy CB batch) | present operation (waits on frame completion credential) |
| `current_frame_in_flight` counter | — |

Note: the former `copy_to_swapchain_completed_semaphores[N]` (copy→present signaling) is removed — "copy complete" is subsumed by `timeline@4`.

**Rationale**: The frame-completion submit is the single point where all frame lifecycle signals are produced; the provider is a thin swapchain shell that records the copy and presents, never owning or interpreting frame lifecycle state.

### Decision 2: `GpuContext` as separate SHARED DLL

**Chosen**: `engine/GpuContext/` built as a `SHARED` library, linked by `engine.dll` and standalone tests.

**Rationale**: Follows the `Core` DLL pattern. Makes the dependency boundary explicit: `GpuContext` depends only on Vulkan, VMA, SDL3 (loadlibrary), and `Core` (`flagbits.h`). `engine.dll` depends on `GpuContext`. Tests can link `GpuContext` alone for pure compute workloads.

**Module contents**:
```
engine/GpuContext/
├── gpu_context_export.h          # GPU_CONTEXT_API macro
├── GpuContext.h / .cpp            # Aggregation class
├── DeviceInterface.h / .cpp       # Moved from Render/RenderSystem/
├── AllocatorState.h / .cpp        # Moved from Render/RenderSystem/
├── MemoryTypes.h                  # Moved from Render/Memory/
├── MemoryAllocation.h / .cpp      # Moved from Render/Memory/
└── CMakeLists.txt
```

**What stays in Render**: `ImageUtils.h`, `ImageUtilsFunc.h`, `ComputeBuffer`, `DeviceBuffer`, `Texture`, pipeline types — these are engine-level GPU resource abstractions, not bare Vulkan management.

**`MemoryTypes.h` dependency on Core**: `MemoryTypes.h` uses `Flags<>` from `Core/flagbits.h`. This is the only Core dependency of GpuContext — acceptable since `Flags<>` is a basic utility template.

### Decision 3: `DeviceInterface` headless mode (if-checks, not strategy)

**Chosen**: Simple `if (window == nullptr)` checks within `DeviceInterface` rather than a strategy interface.

**Rationale**: Only 2 call sites differ (instance extensions list, surface creation). Physical device scoring just skips present support check. The code path is localized to construction — not scattered across frame-level operations like `FrameManager`. An `ISurfaceFactory` interface would add abstraction for negligible benefit.

**Headless behavior**:
- `CreateInstance`: Skip `VK_KHR_surface` and platform surface extensions
- `CreateSurface`: Entirely skipped
- `GetPhysicalDevice`: Don't require present queue; don't query swapchain support
- `CreateDevice`: Present queue index remains `std::nullopt`
- `CreateCommandPool`: Don't create present command pool
- Graphics queue, compute queue, transfer queue: **All created normally** — these are hardware capabilities independent of surface

### Decision 4: `RenderSystem::GetSwapchain()` removal

**Chosen**: Remove `Swapchain` value member and `GetSwapchain()` accessor from `RenderSystem`. All swapchain logic moves into `SwapchainPresentProvider`. External callers read extent/format from `IPresentProvider`.

**Rationale**: `Swapchain` was only used by `FrameManager` (5 calls) and to provide `GetExtent()`/`GetColorFormat()` to external callers. Those callers now get values from `IPresentProvider`. The `Swapchain` class itself becomes an implementation detail of `SwapchainPresentProvider`.

**Affected external callers** (10 total):
- `CommandBuffer.cpp:292` → `m_system.GetSwapchain().GetExtent()` → `m_system.GetPresentProvider().GetExtent()`
- `ComplexRenderGraphBuilder.cpp:171` → `system.GetSwapchain().GetExtent()` → explicit parameter
- `GUISystem.cpp:116,128` → `render_system.GetSwapchain().GetColorFormat()` → `render_system.GetPresentProvider().GetColorFormat()`
- 6 test files → `rsys->GetSwapchain().GetExtent()` → `rsys->GetPresentProvider().GetExtent()`

### Decision 5: `MainClass::Initialize` headless path (Option A)

**Chosen**: `RenderSystem(std::weak_ptr<SDLWindow>{})` — null window signals headless mode. No separate init method, no config struct.

**Rationale**: Minimal API surface change. `RenderSystem` already stores window as `std::weak_ptr`; null weak_ptr naturally means "no window." Behavior divergence happens in `Create()` when `IPresentProvider` type is selected.

### Decision 6: Frame-completion batch — main CB + copy CB in one submit

**Chosen**: `SubmitFrame` submits `{main render CB, copy CB}` as one `vkQueueSubmit2` batch:
- `waits`: `own@2` (staged upload complete), `prev@4` (previous frame fully complete), `image_acquired` (async acquire, **`eAllTransfer` stage mask**)
- `signals`: `own@4` (frame complete) — timepoint 3 disappears; the timeline value jumps 2 → 4 (timeline semaphores allow skip-value signaling)
- `fence`: `command_executed_fences[fif]`

**Semantics of the acquire gate**: `image_ready` is gated at `eAllTransfer` only — a *stage latch*, not a *batch latch*. Graphics and compute stages appear earlier in the Vulkan pipeline order than `eTransfer`, so the main render may proceed before acquire completes; only transfer-stage commands (the copy blit) are gated. `eAllCommands` MUST NOT be used (it would gate the whole batch including the main render). Corollary: the main CB must not contain transfer-stage passes — transfer work goes through `SubmissionHelper` or the copy CB.

**Headless**: `PrepareCopy` returns `nullptr`; the batch degrades to `{main CB}` and still signals `own@4` + fence — the sync chain closes unconditionally, no deadlock.

### Decision 7: RenderGraph records only — submission belongs to the frame-completion point

**Chosen**: `RenderGraph::Execute` no longer submits. It records passes into the main CB (begin / RecordAllPasses / end) and returns; the caller then calls `RenderSystem::CompleteFrame(final_rtt, last_access)`, which triggers `SubmitFrame`.

**Rationale**: The render layer (RenderGraph, physics) must not know about the present target — the swapchain is none of its business. Splitting "record" from "submit" at the Execute call site is the seam between the render layer and the presentation layer. The main CB's begin/end/submit lifecycle belongs to FrameManager.

**Multi-queue evolution**: when RenderGraph implements affinity-based multi-queue submission (graphics/compute/transfer groups, per the RenderGraph.cpp note), it will submit its groups itself and signal a "render complete" signal supplied by FrameManager. `SubmitFrame` then waits `{render complete, image_ready}` and submits only the copy CB — the structure is unchanged, only the wait objects and CB list change. The presentation layer needs no other interface change.

### Decision 8: `AcquireNextImage` contract — MUST signal `image_ready_semaphore`

**Chosen**: `AcquireNextImage` returns the target index and MUST signal `image_ready_semaphore` when the image becomes available:
- Windowed: `vkAcquireNextImageKHR` signals it (async; the GPU-side batch gate waits on it, CPU never blocks)
- Headless: a no-op empty `submit2` signals it (simulating a real acquire; the batch gate is immediately satisfied)

`~0u` means the swapchain is out of date; `StartFrame` MUST trigger `UpdateSwapchain()` + retry instead of propagating the sentinel into framebuffer indexing.

**Rationale**: A synchronous acquire (internal fence + `waitForFences`) would block the CPU and break the frames-in-flight pipeline. The binary-semaphore contract keeps the interface explicit and branch-free for FrameManager.

## Risks / Trade-offs

- **Build breakage from moved files**: All includes of `AllocatorState.h`, `DeviceInterface.h`, `MemoryTypes.h`, `MemoryAllocation.h` change paths. → Mitigation: systematic search-and-replace guided by compiler errors; ~30 affected files.
- **Reflection generator**: The Python/libclang generator parses engine headers. Moving files may affect include paths the reflection tool uses. → Mitigation: check `reflection_parser/` config for hardcoded paths; update if needed.
- **`AllocatorState` constructor signature change**: All callers pass `RenderSystem&` today; must change to `DeviceInterface&`. → Mitigation: at each call site, replace `render_system` with `render_system.GetDeviceInterface()` or pass `GpuContext` member.
- **`SwapchainPresentProvider` owns swapchain lifecycle**: Currently `RenderSystem::CreateSwapchain()` handles swapchain creation/recreation. Moving this to `SwapchainPresentProvider::Recreate()` means resize events must route through `IPresentProvider`. → Mitigation: `RenderSystem::UpdateSwapchain()` delegates to `present_provider->Recreate(new_extent)`.
- **Headless mode needs `SDL_INIT_VIDEO` still**: `SDL_Vulkan_LoadLibrary(nullptr)` requires SDL video subsystem initialized. → Acceptable: no window is created, just the library loaded.
- **Cross-DLL dispatch loader**: Vulkan-Hpp's `VULKAN_HPP_STORAGE_SHARED` does NOT create DLL import/export on Clang/MinGW (only MSVC). Each DLL gets its own `vk::detail::defaultDispatchLoaderDynamic` copy. `DeviceInterface` (in GpuContext.dll) initializes its copy; engine.dll's copy stays empty. → Mitigation: `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` in `MainClass.cpp`, then in `RenderSystem::Create()` call `VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, vkGetInstanceProcAddr)` followed by `init(device)`. `init(device)` alone crashes with a null `vkGetDeviceProcAddr` DEP violation.
- **Headless sync-chain deadlock (fixed by design)**: an earlier iteration made `HeadlessPresentProvider::CompleteFrame` a no-op that skipped the only submit signaling `timeline@4` + fence → GPU stall from frame 2, CPU hang from frame 4. The single-frame offscreen test cannot detect it. → Mitigation: `SubmitFrame` unconditionally submits the frame-completion batch (copy CB optional); the sync chain is a FrameManager invariant, not adapter behavior. Add a multi-frame (≥4 frames + readback) headless test.
- **Batch-level acquire gate**: `image_ready` waits at `eAllTransfer` — a stage latch. Graphics/compute stages run ahead; only transfer-stage commands wait. MUST NOT use `eAllCommands`. The main CB must not contain transfer-stage passes.
- **`RenderGraph::Execute` semantic change (API break)**: it now only records instead of record+submit. All call sites (tests, examples) must be updated; consider renaming to `RecordIntoMainCommandBuffer`.
- **Main CB `end()` ownership**: `SubmitFrame` ends the main CB; callers must stop calling `end()` themselves, or begin/end converge into `BeginMainCommandBuffer()` / `SubmitFrame`.
- **`MainClass::RunOneFrame` headless crash**: `input->ProcessEvent` and `window->GetSize()` dereference nulls when `--headless` is set (Initialize skips window/Input creation). → Mitigation: headless branch in `RunOneFrame`; obtain size from `GetPresentProvider().GetExtent()`.

## Open Questions

*(None — final design resolved through the review session: frame-completion batch, frame completion credential, async acquire contract, RenderGraph record-only.)*
