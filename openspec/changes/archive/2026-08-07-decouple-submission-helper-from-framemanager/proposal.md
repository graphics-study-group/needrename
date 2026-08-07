## Why

`SubmissionHelper` (`engine/Render/RenderSystem/SubmissionHelper.h`) currently "parasitizes" the `FrameManager` frame-throttling protocol for its synchronization safety: it calls `m_system.GetFrameManager().GetFrameSemaphore()` directly for the timeline signal and relies on the `StartFrame` fence wait to guarantee one-time command buffer reuse safety. Once this protocol is deviated from (consecutive submissions, mixing immediate/deferred submission, resetting without submitting), silent data races or validation-layer errors occur. This blocks its future migration into `GpuContext` (standalone headless physics usage) — the synchronization lifecycle must be owned by the helper itself first.

## What Changes

- **BREAKING**: `ExecuteSubmission` gains a `vk::SemaphoreSignalInfo` parameter (the caller supplies the timeline signal; on an empty batch the helper performs a CPU-side signal). The `OnPreMainCbSubmission` thin wrapper is removed; `FrameManager::SubmitFrame` calls `ExecuteSubmission(GetSignalInfo(2))` directly
- Constructor changes from `RenderSystem &` to `(DeviceInterface &, AllocatorState &)`; the header no longer depends on `RenderSystem.h` / `FrameSemaphore.hpp`
- An explicit state machine (`Reset` / `Submitted`) is introduced: calling `ExecuteSubmission` or `ExecuteSubmissionImmediately` while `Submitted`, or calling `OnFrameComplete` with unsubmitted operations pending, throws `std::runtime_error`
- Staging accounting is split per batch (`m_pending_staging` / `m_inflight_staging`), fixing the latent bug where `ExecuteSubmissionImmediately` cleared the whole container and could free staging still in use by a deferred batch
- Destructor defense: if a deferred batch is still pending at destruction, wait on its fence first
- Single-thread usage constraint is declared (no locking; multithreading is designed separately when async resource loading lands)
- New `test/submission_helper_test.cpp`: constructs `SubmissionHelper` headlessly without `FrameManager`, covering the state machine protocol and throw paths
- Physics call sites (`PhysicsScene.cpp`) are untouched; the `FrameManager` frame protocol (timeline @2/@4) is behavior-preserving

## Capabilities

### New Capabilities

- `submission-helper-sync`: The `SubmissionHelper` synchronization lifecycle protocol — state machine (`Reset`/`Submitted`), parameterized submission signal, per-batch staging accounting, and fail-fast on protocol violations, making it safe to use without the `FrameManager` frame throttle

### Modified Capabilities

(None — no existing capability's REQUIREMENTS change; `FrameManager` behavior is equivalent and physics call sites are untouched)

## Impact

- Code: `engine/Render/RenderSystem/SubmissionHelper.h` / `SubmissionHelper.cpp` (state machine + batch separation + parameterization), `engine/Render/RenderSystem/FrameManager.cpp` (one-line `SubmitFrame` adaptation), new `test/submission_helper_test.cpp`, `test/CMakeLists.txt`
- Dependency direction: `SubmissionHelper` no longer includes `Render/RenderSystem.h`; it depends only on `GpuContext` (DeviceInterface/AllocatorState) and `Render/Memory` (DeviceBuffer/Texture)
- Interface: `ExecuteSubmission` signature changes (breaking), `OnPreMainCbSubmission` removed; class name, method names, file names, and namespace remain unchanged
- Behavior: the `FrameManager` frame protocol (timeline @2 staged-upload-complete) is unchanged; the physics submission channel is unchanged
- Verification: full regression of registered tests (16 in test/ + engine/Tests) plus the new unit test
