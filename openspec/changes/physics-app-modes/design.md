## Context

`PhysicsApp` (`app/physics/`) is a thin, DLL-bounded wrapper over the engine: `Create` initializes `MainClass`, builds bodies through `SceneBuilder`, and drives `Step` / `RenderNextFrame`. Today it is windowed-only and exposes no simulation data to consumers. The engine already has a headless seam: `StartupOptions::headless` makes `MainClass::Initialize` skip the window, `GUI` and `Input`, and `RenderSystem::Create` picks a present provider from `m_window.expired()`. However `HeadlessPresentProvider` (RenderSystem.cpp:69) is a stub that returns `nullptr` from `PrepareCopy` (frames are discarded) and hard-codes a 1920×1080 extent. `FrameManager` owns all frame-lifecycle sync (fences, timeline semaphores, frame-in-flight) and performs the single frame-completion batch (`SubmitFrame`); its `RegisterReadbackCallback` is a buffer-only, 1-2 frame delayed mechanism. Physics runs XpbdGpuSolver on the GPU in all modes (physics needs a Vulkan device even when headless), and `Step` already waits on its physics submission fence plus `WaitForIdle` before/after.

## Goals / Non-Goals

**Goals:**
- Three immutable `AppMode`s that reuse the existing headless seam and add zero runtime branch cost in the common windowed path.
- CPU readback of full rigid-body state (position / rotation / velocities) via `BodyId → slot` mapping, indexed directly into state arrays.
- Opt-in, same-frame render readback in both offscreen and windowed modes.
- Engine facilities general enough for any engine user (pure-function copy recorder, precise frame wait), not PhysicsApp-specific.

**Non-Goals:**
- Runtime mode switching (swapchain↔offscreen hot swap); mode is fixed at create.
- Readback-callback API on `OffscreenPresentProvider` (deferred; provider keeps a simple present-target surface).
- Using `FrameManager::RegisterReadbackCallback` for PhysicsApp's same-frame render readback (its 1-2 frame latency is incompatible with same-frame semantics).
- Writing CPU→GPU (kinematic control upload) or modifying physics solver/scene internals.
- A ctest for windowed mode beyond the finite-frame smoke run (no readback assertion there).

## Decisions

### D1: AppMode is a three-value enum in CreateInfo, immutable

`AppMode { Headless, Offscreen, Windowed }`, `CreateInfo.mode` defaulting to `Windowed`. Chosen over a two-axis `(has_window × needs_render)` model — the three values map exactly to the user's mental model and to driver branches; the fourth axis combination (window without render) is meaningless. Immutable because hot-swapping the present provider / render graph mid-run is a much larger engine feature with no current user.

### D2: BodyId → slot mapping is built explicitly at CommitScene, not assumed by order

`BodyId` is the creation-order index into `SceneBuilder`'s internal body list. It numerically coincides with the physics slot index only incidentally (monotonic slot allocation + FIFO init). Rather than codify that as a contract, `CommitScene` (after `FlushPhysics`, when `Awake` has assigned slots) builds an explicit `vector<uint32_t>` mapping by `PhysicsAdaptor::FindRigidBodyByObjectHandle(handle)`. `SceneBuilder` gains two read-only accessors (`GetBodyCount`, `GetBodyHandle`) and stays free of `PhysicsAdaptor` dependency. This survives future slot-reuse or reorder changes.

### D3: Physics readback uses four independent ReadbackFromDevice buffers, pre-seeded at commit

Four `DeviceBuffer` (`ReadbackFromDevice` = `CopyTo | HostRandomAccess`, auto-mapped in VMA) sized `slot_count × sizeof(vec4)` — one per SoA array (position / rotation / linear / angular velocity). Independent buffers avoid offset arithmetic and give each `BodyStatesView` span a clean pointer. They are allocated at `CommitScene` end (fixed size — the freeze contract guarantees no further allocation). `CommitScene` additionally runs one one-time copy submit (SSBO → staging, same recipe as `Step`) so the staging holds the committed initial state — making state reads legal before the first `Step` (explicit user requirement). `Step` reuses the same recorder inside its physics command buffer after `GPUStep`; because `Step` already waits on its submission fence, the data is valid the moment `Step` returns.

### D4: Physics API is C-shaped: assembled getter + SoA batch view

`GetBodyState(BodyId)` assembles `{vec3 position, quat rotation, vec3 linear_velocity, vec3 angular_velocity}` from the vec4 SoA (rotation is quat xyzw; velocity `.w` is padding). `GetBodyStates()` returns `BodyStatesView { span<const uint32_t> slot_indices; span<const glm::vec3> com_offsets; span<const glm::vec4> positions; ... }`, all indexed by slot index. `com_offsets` is a CPU-side static array collected from `PhysicsAdaptor::GetComOffsetLocal` at commit (GO-local space) — reserved for future URDF imports whose center of mass is not at the object origin; zero for self-built shapes. Batch span keeps the SoA layout (zero conversion) for RL-style full sweeps; the assembled getter serves single-body reads. Positions are COM positions (documented), not GameObject origins.

### D5: Same-frame render readback is recorded in the main command buffer, not a render-graph pass

Two readback patterns already exist in tests (`headless_offscreen_test`, `mrt_test`): a render-graph pass declaring `UseImage(TransferRead)` + `UseBuffer(TransferWrite)`. That route is rejected for PhysicsApp because (a) `ImportExternalResource(DeviceBuffer)` stores a raw `&buffer` (RenderGraphBuilder.cpp:351) — rebuilding the staging on window resize (final RTT is resizable) would dangle, and there is no "update external buffer" semantics; (b) RG buffer deps currently introduce a global barrier. Instead the copy is recorded **in the main CB after `RecordAllPasses`**: a pre-barrier `eGeneral → eTransferSrcOptimal`, `copyImageToBuffer`, then a post-barrier back to `eGeneral`. The restoration keeps `CompleteFrame(present_texture, ShaderRandomWrite)`'s `last_access` contract intact (the swapchain blit's source-layout derivation is unaffected). `GetRenderOutput()` then calls the new `FrameManager::WaitForFrameCompletion()` and maps the staging — because the copy executes in-order inside the frame-completion batch, fence-signaled ⟹ copy complete ⟹ coherent memory is CPU-visible with no extra barrier.

### D6: RecordCopyImageToBuffer is a pure function (caller-controlled enable)

Placed beside `GetImageLayout`/`GetAccessFlags` (MemoryAccessHelper.hpp or a sibling util), it is a stateless recorder with parameters `(cb, texture, dst_buffer, last_access)`. A pure function needs no enablement flag — whoever calls it opts in; unused means zero cost. It serves three consumers with differing call patterns: `PhysicsApp` (per-frame when readback enabled), `OffscreenPresentProvider::PrepareCopy` (every rendered frame), and any future engine user. Rejected alternatives: a `RenderSystem` state service (`SetReadbackTarget`) that adds per-frame enable checks and lifecycle state for a feature only PhysicsApp needs.

### D7: FrameManager::WaitForFrameCompletion() for precise waits

A new `FrameManager` method waits on the fence of the most recently submitted frame. Because `SubmitFrame` → `pimpl->CompleteFrame()` advances the frame-in-flight counter, the fence must be captured at submit time (`last_submitted_fence`), not looked up via `GetFrameInFlight()`. Avoids `RenderSystem::WaitForIdle()`'s full-drain cost on a per-frame readback path. No-op when no frame was ever submitted.

### D8: Render readback is opt-in via SetRenderReadbackEnabled, not per-call variant

`SetRenderReadbackEnabled(bool)` defaults `false`; `RenderNextFrame` records the copy only when enabled. `GetRenderOutput()` is a separate method (per earlier agreement). Enabled at any phase; effect starts from the first `RenderNextFrame` after commit. Rejected a `RenderNextFrameAndReadback()` variant because the flag form supports "preheat N frames then read" and keeps a single render method; a variant is a 3-line wrapper later if wanted. When disabled, no staging is allocated at all (staging is created on first enable and retained after disable to avoid thrash; rebuilt on present-extent change for windowed resize).

### D9: OffscreenPresentProvider owns host-visible targets, lazily, no callback this round

The provider allocates `GetImageCount()` `ReadbackFromDevice` buffers sized to the present extent, **lazily on first `PrepareCopy`** — so pure-physics headless loops that never render allocate nothing (one class covers both headless flavors). `Present` stays a no-op returning `false`. Host-visible targets were chosen over device-local images + blit because a future readback callback only needs to poll a fence and map — the target shape never changes; blit scaling is unnecessary since the target equals the RTT extent. The extent hard-code is removed: `RenderSystem` gains a defaulted `headless_extent` ctor parameter (`{1920,1080}` keeps existing call sites compiling), fed from `StartupOptions::resol_x/y` by `MainClass::Initialize`.

### D10: Mode branches live in PhysicsApp; Step keeps its WaitForIdle

- `RenderNextFrame`: mode `Headless` → throw; mode `Offscreen` → run the render frame minus the input section (SDL polling, input update, SPACE toggle); mode `Windowed` → current behavior.
- `CommitScene`: `Headless` skips render-graph build and model-matrix bridge; `SceneBuilder` gets a `with_visuals` ctor flag (skips mesh children + `ResolveColor`).
- `ShouldQuit`: returns `false` in `Headless`/`Offscreen`.
- `Step`: keeps pre/post `WaitForIdle` in all modes — the physics submission fence already guarantees GPU physics completion; the waits are cheap on an idle device (fast-path queue check), keep one code path, and remain correct if physics/rendering step rates ever diverge.

### D11: Test strategy — three executables, example retired

`example/physics_example` is deleted; its scene builders move into `test/physics_app_windowed_test.cpp`, which supports two launcher modes: with a frame-count argument it runs that many frames and auto-resumes after commit (ctest path); without arguments it runs indefinitely starting paused with SPACE resume (manual UX identical to the old example — requires a display). `physics_app_headless_test` covers mode-1 physics readback, mapping uniqueness, negative throws. `physics_app_offscreen_test` covers mode-2 render readback (dimensions, frame_id progression, disabled/enabled throws). All three link `Engine` + `PhysicsApp`. Windowed test deliberately has no readback assertion (kept as a smoke run).

## Risks / Trade-offs

- [Same-frame readback serializes the CPU on the GPU] → Documented trade-off: `GetRenderOutput()` waits the frame fence, collapsing frame-in-flight depth to 1 for callers who use it. Non-users of `GetRenderOutput` pay nothing. This is the explicit cost of "same-frame" semantics.
- [Main-CB copy could break the `CompleteFrame` layout contract] → The post-copy barrier restores `eGeneral` so `PrepareCopy`'s `last_access`-derived source layout stays correct; reuse of the `RecordCopyCommand` barrier recipe.
- [Staging dangling on window resize] → Rebuild staging when present extent changes (checked in `RenderNextFrame` where extent is already read). Mode 2 extent is fixed, so this only affects windowed mode.
- [Skip frame (swapchain out of date) leaves no new data] → `GetRenderOutput` returns the previous successful frame; `frame_id` lets callers detect staleness.
- [Mapping correctness depends on Awake assigning slots in create order] → Mitigated by building the mapping explicitly via `FindRigidBodyByObjectHandle` rather than assuming equality; an assert fires if any body lacks a slot at commit.
- [OffscreenPresentProvider lazy allocation + `Recreate` sizing] → Allocation follows the then-current extent; `Recreate` updates stored extent for future allocations (fixed-size in practice for offscreen).
- [Deleting the example is breaking for anyone running it] → The windowed test reproduces its behavior and content; both the CMake entry and the directory are removed together.

## Migration Plan

1. Engine land first (independent, testable in isolation): `RecordCopyImageToBuffer` util, `FrameManager::WaitForFrameCompletion`, `RenderSystem` `headless_extent` ctor param + `MainClass` forwarding.
2. Replace `HeadlessPresentProvider` with `OffscreenPresentProvider` (lazy host-visible targets + real copy). Existing headless tests (`headless_offscreen_test`, `headless_compute_test`) remain green; `mrt_test` unaffected (uses its own RG pass + registered callback).
3. PhysicsApp mode + physics readback (mapping, staging, `Step` copy, `GetBodyState(s)`).
4. PhysicsApp render readback (`SetRenderReadbackEnabled`, `GetRenderOutput`, main-CB copy).
5. Delete `example/physics_example`, add the three test executables (relocating scene builders).
6. Rollback: revert per-layer commits; windowed behavior is unchanged by every intermediate step (mode defaults to `Windowed`, readback defaults off).

## Open Questions

- None blocking. Implementation details to settle during coding: exact placement of `RecordCopyImageToBuffer` (MemoryAccessHelper.hpp vs new header), the `SceneBuilder` accessor signatures, and the `PhysicsApp` CMake target name for test linking (to be read from CMake at implementation time).
