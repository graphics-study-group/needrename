## Context

`SubmissionHelper` is a "deferred resource upload/clear queue": the `Enqueue*` phase allocates a staging buffer on the CPU side, memcpys the data, and records a closure; the submission phase records closures into a one-time command buffer and submits asynchronously; the reap phase waits on a fence, frees staging, and reclaims the command buffer.

It currently holds a `RenderSystem &m_system` service locator. Of the four uses, only one is a real system dependency: `GetDevice()`, `GetAllocatorState()`, and `GetDeviceInterface()` are all plain forwards into the GpuContext module, while `m_system.GetFrameManager().GetFrameSemaphore()` (the timeline @2 signal) is the only `FrameManager` coupling. Synchronization safety currently depends on two external protocols:

1. **Command buffer reuse safety**: `FrameManager::StartFrame`'s fence wait guarantees the previous one-time CB batch has finished (parasitic ride — `SubmissionHelper.cpp:369` states *"We don't need to wait for anything thanks to fences in the frame manager"*);
2. **Staging release safety**: the `m_completion_fence` wait in `OnFrameComplete` (this one is already self-owned).

Any deviation from the protocol (consecutive submissions, mixing immediate/deferred submission, resetting without submitting) causes silent UB. All current Enqueue call sites (`MaterialInstance::Instantiate`, `StaticMeshResourceManager::EnsureReadyImpl`, `SceneDataManager`, `PhysicsScene`) run during the frame preparation stage, and the `AcquireAsyncImpl` comment shows async loading is a future plan — the protocol is currently unviolated only by calling-order coincidence. In particular, `ExecuteSubmissionImmediately` ends with `m_pending_dellocations.clear()` (cpp:408), which would wrongly free a deferred batch's staging when mixed.

This change paves the way for "migrate to GpuContext, standalone headless physics" by first making the synchronization lifecycle self-owned.

## Goals / Non-Goals

**Goals:**
- Remove all `FrameManager` dependencies from `SubmissionHelper` (parameterized timeline signal; constructor takes `(DeviceInterface&, AllocatorState&)`)
- Turn synchronization safety from an "external protocol" into an "internal structural guarantee": explicit state machine + fail-fast
- Fix the latent bug where full-container staging clearing could wrongly free a deferred batch's staging
- Keep the `FrameManager` frame protocol (timeline @2/@4) fully behavior-preserving; physics call sites untouched
- Lock the state machine protocol in with a standalone unit test

**Non-Goals:**
- No file migration into GpuContext, no class/method/namespace renaming (deferred to a later migration change)
- No multithreaded Enqueue support (single-thread constraint declared; designed separately when async loading lands)
- No change to the `FrameManager` frame protocol semantics
- No relocation of DeviceBuffer/Texture types

## Decisions

### D1: Remove the FrameManager dependency entirely (parameterized timeline signal)

`ExecuteSubmission` becomes `ExecuteSubmission(vk::SemaphoreSignalInfo signal_info)`. The signal is supplied by the caller:

```cpp
// FrameManager::SubmitFrame (former OnPreMainCbSubmission call site)
m_submission_helper->ExecuteSubmission(this_timeline_semaphore.GetSignalInfo(2));
```

- On an empty batch, the helper performs `device.signalSemaphore(signal_info)` internally (CPU-side signal, advancing the timeline so the main batch's wait cannot deadlock); with a non-empty batch it builds `vk::SemaphoreSubmitInfo{sem, value, eAllTransfer}` from the signal info and signals with the submission
- Parameter type is `SemaphoreSignalInfo` rather than `SemaphoreSubmitInfo`: the CPU-signal branch can use it directly (no stage semantics), and the GPU branch supplies the stage internally (a signal's stage only gates when the signal action executes; the batch contains only transfer commands, so `eAllTransfer` matches current behavior); `FrameSemaphore::GetSignalInfo(2)` returns exactly this type
- Alternative considered: `ExecuteSubmission` returns bool and the caller performs the CPU signal — pushes the "configure synchronization" duty back to the caller; not adopted

### D2: Constructor `(DeviceInterface &, AllocatorState &)`

The remaining three uses of `m_system` are plain forwards, so keeping `RenderSystem&` has no justification. `FrameManager::impl::Create` becomes `std::make_unique<SubmissionHelper>(m_system.GetDeviceInterface(), m_system.GetAllocatorState())`. The header no longer includes `Render/RenderSystem.h` or `Render/RenderSystem/FrameSemaphore.hpp`.

### D3: Explicit state machine (Reset / Submitted)

```
State: Reset (submittable) ⇄ Submitted (deferred batch pending, awaiting reap)
```

| Method | Precondition | Empty-batch behavior | Resulting state |
|---|---|---|---|
| `Enqueue*` | any | — | unchanged |
| `ExecuteSubmission(signal)` | must be Reset, else throw | CPU signal, no submit | non-empty → Submitted; empty → unchanged |
| `ExecuteSubmissionImmediately()` | must be Reset, else throw | return (immediate mode has no timeline protocol) | unchanged (self-contained) |
| `OnFrameComplete()` | Submitted → normal reap; Reset + pending non-empty → throw; Reset + empty → idempotent return | — | → Reset |

- Exception type is `std::runtime_error`, consistent with `FrameManager::assert_in_frame`; messages state the protocol (e.g. *"ExecuteSubmission called while a previous batch is still pending. Call OnFrameComplete() first."*)
- Precondition checks happen before any allocation/submit, so a throw has no resource side effects
- The state transition happens only after a successful submit2; on failure the state stays Reset
- `OnFrameComplete` returns idempotently when Reset and empty (`FrameManager` calls it every frame; first frame / no-submit frames are normal); Reset with pending non-empty throws (Enqueue without submitting before reset is a protocol violation — exploration confirmed all current Enqueues happen in the frame preparation stage, so pending is always empty at `OnFrameComplete`; no false positives)
- Destructor: if Submitted, `waitForFences` first (RenderSystem member destruction order guarantees the device is still alive)
- Alternative considered: option A "auto-wait the previous batch fence before submitting" (structural guarantee, safe under any call sequence) — larger implementation surface and conflicts with "minimal change, stay in place"; the state machine is a superset of A, and if headless physics ever needs the deferred path, the state machine can be upgraded to auto-wait without conflict

### D4: Per-batch staging accounting

```
m_pending_staging    staging enqueued but not yet submitted (filled by Enqueue*)
m_inflight_staging   staging submitted by the deferred path, awaiting OnFrameComplete
```

- `ExecuteSubmission`: `move(m_pending_staging → m_inflight_staging)` → submit
- `ExecuteSubmissionImmediately`: takes `m_pending_staging` (all currently pending) → submit → wait → clears only its own batch → self-contained
- `OnFrameComplete`: after the wait, clears `m_inflight_staging` and reclaims `m_one_time_cb`
- Declared semantics: Immediately = "submit all currently pending operations and wait for completion" — operations already enqueued by the render layer are carried along and submitted early; results are equivalent (closures are self-contained), only the timing moves earlier; the render layer's subsequent `ExecuteSubmission` then takes the empty-batch CPU-signal path and the main batch's wait still passes

### D5: Single-thread usage constraint (no locking)

The header documents "Must be called from a single thread (the frame loop thread). Not thread-safe." Locking would be false safety (state-machine checks and container operations race under concurrency); real multithreaded Enqueue requires a redesign (thread-safe submission queue) and belongs to the `AcquireAsyncImpl` async-loading work.

### D6: Header protocol documentation

Class-level documentation carries the state machine protocol: the two-state transitions, per-method preconditions, throw behavior, single-thread constraint, Immediately semantics, and empty-batch CPU-signal semantics. `OnPreMainCbSubmission` is removed.

## Risks / Trade-offs

- ["Cross-frame Enqueue" pattern becomes forbidden (enqueue at frame tail for next-frame submission → `OnFrameComplete` throws)] → No current usage (exploration confirmed all Enqueues happen during frame preparation); if async loading ever needs arbitrary-time Enqueue, that is an explicit protocol-extension decision — leave a comment at the `OnFrameComplete` check
- [Render-layer Enqueue gets submitted early by physics `ExecuteSubmissionImmediately` (D4 semantics)] → Closures are self-contained and results are equivalent, timing only moves earlier; physics Immediately already blocks, so no added cost
- [State-machine throws only cover same-thread protocol violations; concurrency races are out of scope] → D5 declares the constraint; multithreading is a future standalone design
- [`OnFrameComplete` pending check may flag future new call sites] → Protocol documentation plus a comment stating "Enqueue operations must be submitted within the same frame"
- [Behavior-preserving refactor regression] → Full regression of registered tests (16 in test/ + engine/Tests) plus the new unit test locking the state machine

## Migration Plan

1. Write `submission_helper_test.cpp` first (headless `GpuContext`, direct construction, covering all state machine paths) as a safety net for the refactor
2. Refactor `SubmissionHelper.h/.cpp` (state machine + batch separation + parameterization + constructor signature)
3. Adapt the one-line call site in `FrameManager.cpp`
4. Full test regression (windowed + headless + engine/Tests)
5. Rollback: no data migration involved; code-level revert (restore the SubmissionHelper implementation and the FrameManager call site)

## Open Questions

**RESOLVED** — Enqueue call-site timing confirmed by exploration (all engine-internal Enqueues precede `ExecuteSubmission`):

| Enqueue call site | Frame-loop phase | Relative to `ExecuteSubmission` |
|---|---|---|
| `MaterialInstance::Instantiate` (texture upload/clear) | Component `Awake` / resource creation (`FlushCmdQueue`, `LoadAssetsInQueue`) | before ✓ |
| `StaticMeshResource::Submit` | `Awake` (eager) or `RendererManager::FilterAndSortRenderers` (non-eager, render-graph recording) | before ✓ |
| `SceneDataManager` default light map clear | initialization | before ✓ |
| `PhysicsScene::SyncGpuBuffers` (Enqueue + `ExecuteSubmissionImmediately`) | `FlushPhysics` / `GPUStep` | before ✓ |
| test Enqueue sites (e.g. `skybox_test`) | initialization, followed by `ExecuteSubmissionImmediately` | before ✓ (self-contained) |
| readback callbacks (user code, `FrameManager::CompleteFrame`) | invoked inside `CompleteFrame` **before** `OnFrameComplete` | **after ⚠️** |

- Engine-internal call sites are all safe: recording-phase `EnsureReady` (`FilterAndSortRenderers` for non-eager meshes) still precedes `ExecuteSubmission` because recording happens before `SubmitFrame` in `RunOneFrame`. Add a comment at the `OnFrameComplete` check stating this dependency.
- The only risk window is readback callbacks (user code) Enqueueing inside `CompleteFrame` before `OnFrameComplete` — current callbacks (`mrt_test`) do not Enqueue, but the interface allows it. This is the intended "no cross-frame Enqueue" contract: document that readback callbacks must not perform uploads.
- Swapchain-recreation skip frames (StartFrame returning `~0u`) skip `CompleteFrame` entirely, so no `OnFrameComplete` throw; pending operations are submitted by the next frame's `ExecuteSubmission` as in current behavior.
