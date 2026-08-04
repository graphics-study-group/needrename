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

### Decision 1: `IPresentProvider` Strategy Pattern

**Chosen**: `IPresentProvider` interface with `SwapchainPresentProvider` and `HeadlessPresentProvider` implementations.

**Rationale**: Without this, `FrameManager::StartFrame`, `FrameManager::PresentToFramebuffer`, `RenderSystem::CompleteFrame`, `CommandBuffer::DrawRenderers`, and `ComplexRenderGraphBuilder` would each need `if (headless)` branches scattered across 5+ files. The Strategy pattern localizes the variation into two implementations of a single interface.

**Alternatives considered**:
- *If-checks everywhere*: Rejected — leads to brittle, hard-to-test code with hidden coupling.
- *Nullable swapchain + headless-swapchain stub*: Requires `Swapchain` class to handle both modes internally. Rejected — mixes orthogonal concerns; headless "swapchain" has no images, no present, no barrier to define.

**Interface location**: `engine/Render/RenderSystem/IPresentProvider.h` — allows `RenderTargetTexture` in `CompleteFrame` signature without cross-module dependency, since `RenderSystem` state classes already have access to Render types.

### Decision 1b: `FrameSyncInfo` — sync primitives passed as pre-built submit arrays

**Chosen**: `CompleteFrame` receives a `FrameSyncInfo` struct containing two arrays of `vk::SemaphoreSubmitInfo` (wait/signal) plus a `vk::Fence`.

```cpp
struct FrameSyncInfo {
    std::array<vk::SemaphoreSubmitInfo, 2> wait;    // [0]=timeline, [1]=image_acquired
    std::array<vk::SemaphoreSubmitInfo, 2> signal;  // [0]=timeline, [1]=copy_completed
    vk::Fence fence;
};
```

**Rationale**:
- `vk::SubmitInfo2` natively takes wait/signal arrays — zero conversion, the provider is a dumb pass-through into `submit2`.
- Arrays are extensible: future multi-queue or async compute work grows the arrays without changing the interface signature.
- The provider never interprets semaphore semantics (timeline values, timepoints) — FrameManager pre-computes them from its `FrameSemaphore` state.

**Rejected alternatives**:
- *Individual semaphore parameters* (`vk::Semaphore image_acquired, vk::Semaphore timeline, uint64_t wait_value, ...`): too many parameters, breaks when sync count changes.
- *Provider builds submit info from semantic handles*: leaks timeline bookkeeping into the provider, blurs the ownership boundary.

### Decision 1c: `SwapchainPresentProvider` owns copy command buffers

**Chosen**: `copy_to_swapchain_command_buffers` moves from `FrameManager` into `SwapchainPresentProvider`, allocated from the graphics command pool during `Initialize()`. `CompleteFrame` records the blit into one of its own buffers.

**Rationale**: The copy buffers exist only to blit the final RTT into a swapchain image — a pure swapchain concern. Headless mode never allocates them.

### Decision 1d: `FrameManager::PresentToFramebuffer` is restored, not stubbed

**Chosen**: `PresentToFramebuffer` keeps its full logic but delegates the Vulkan blit/submit/present to `IPresentProvider::CompleteFrame`. FrameManager retains ALL synchronization ownership.

**Rationale**: `PresentToFramebuffer` is not merely "blit + present" — it is the frame-synchronization hub:
1. Builds `FrameSyncInfo` from its internal semaphores (timeline, image_acquired, copy_completed, fence)
2. Calls `m_present_provider->CompleteFrame(...)` — the provider records blit, submits with `FrameSyncInfo`, presents
3. Calls `pimpl->CompleteFrame()` — readback processing, `current_frame_in_flight++`, timeline `EndFrame()`, submission helper tick
4. Returns `needs_recreating` from the provider

Stubbing it (as done in an earlier iteration) broke the timeline state machine: `current_frame_in_flight` never advanced and timeline timepoints desynchronized, causing `waitForFences` deadlock and `vkSignalSemaphore` value-order violations.

### Decision 1e: Synchronization ownership boundary

**Chosen**:

| Owned by FrameManager | Owned by IPresentProvider |
|---|---|
| `timeline_semaphores[3]` (GPU-GPU ordering) | `VkSwapchainKHR` + `swapchain_images[]` |
| `image_acquired_semaphores[3]` (acquire→copy) | extent, color format, image count |
| `copy_to_swapchain_completed_semaphores[N]` (copy→present) | `copy_to_swapchain_command_buffers[N]` |
| `command_executed_fences[3]` (CPU-GPU) | internal present-completion signaling |
| `current_frame_in_flight` counter | — |

**Rationale**: Synchronization primitives are per-frame-in-flight resources that interlock with `FrameManager`'s submit pipeline (`SubmitMainCommandBuffer`, readback). The provider is a thin swapchain data + operation shell: it provides images/barriers/format, executes acquire/present, and records the copy blit — but never owns or interprets frame lifecycle state.

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

## Risks / Trade-offs

- **Build breakage from moved files**: All includes of `AllocatorState.h`, `DeviceInterface.h`, `MemoryTypes.h`, `MemoryAllocation.h` change paths. → Mitigation: systematic search-and-replace guided by compiler errors; ~30 affected files.
- **Reflection generator**: The Python/libclang generator parses engine headers. Moving files may affect include paths the reflection tool uses. → Mitigation: check `reflection_parser/` config for hardcoded paths; update if needed.
- **`AllocatorState` constructor signature change**: All callers pass `RenderSystem&` today; must change to `DeviceInterface&`. → Mitigation: at each call site, replace `render_system` with `render_system.GetDeviceInterface()` or pass `GpuContext` member.
- **`SwapchainPresentProvider` owns swapchain lifecycle**: Currently `RenderSystem::CreateSwapchain()` handles swapchain creation/recreation. Moving this to `SwapchainPresentProvider::Recreate()` means resize events must route through `IPresentProvider`. → Mitigation: `RenderSystem::UpdateSwapchain()` delegates to `present_provider->Recreate(new_extent)`.
- **Headless mode needs `SDL_INIT_VIDEO` still**: `SDL_Vulkan_LoadLibrary(nullptr)` requires SDL video subsystem initialized. → Acceptable: no window is created, just the library loaded.
- **Cross-DLL dispatch loader**: Vulkan-Hpp's `VULKAN_HPP_STORAGE_SHARED` does NOT create DLL import/export on Clang/MinGW (only MSVC). Each DLL gets its own `vk::detail::defaultDispatchLoaderDynamic` copy. `DeviceInterface` (in GpuContext.dll) initializes its copy; engine.dll's copy stays empty. → Mitigation: `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` in `MainClass.cpp`, then in `RenderSystem::Create()` call `VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, vkGetInstanceProcAddr)` followed by `init(device)`. `init(device)` alone crashes with a null `vkGetDeviceProcAddr` DEP violation.
- **PresentToFramebuffer is the sync hub**: It must NOT be stubbed or simplified. It owns timeline state transitions (timepoint advance, `EndFrame()`), frame-in-flight advance, readback submission, and swapchain-recreation signaling. Earlier iteration stubbed it and caused `waitForFences` deadlock + `vkSignalSemaphore` value-order errors. → Mitigation: restore full body, delegate only Vulkan blit/submit/present to `IPresentProvider::CompleteFrame`.

## Open Questions

*(None — all design decisions resolved through grilling session.)*
