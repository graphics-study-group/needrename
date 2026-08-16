## 1. SubmissionHelper Interface Changes (header)

- [x] 1.1 Change the constructor to `SubmissionHelper(DeviceInterface &di, AllocatorState &allocator)`; remove the `RenderSystem &m_system` member and the `Render/RenderSystem.h` / `Render/RenderSystem/FrameSemaphore.hpp` includes
- [x] 1.2 Add the `vk::SemaphoreSignalInfo` parameter to `ExecuteSubmission`; remove the `OnPreMainCbSubmission` declaration
- [x] 1.3 Add class-level protocol documentation: Reset/Submitted state machine, per-method preconditions, `std::runtime_error` on protocol violations, single-thread usage constraint, `ExecuteSubmissionImmediately` "submits all currently pending operations" semantics, and empty-batch CPU-signal semantics
- [x] 1.4 Update per-method `@brief`/`@warning` docs (`ExecuteSubmission` parameter description, `OnFrameComplete` reap protocol, destructor behavior)

## 2. SubmissionHelper Implementation Changes (source)

- [x] 2.1 Add a batch state enum (`Reset`/`Submitted`) and per-batch staging containers (`m_pending_staging` / `m_inflight_staging`) to `impl`, replacing `m_pending_dellocations`
- [x] 2.2 Route the three `Enqueue*` methods to push staging into `m_pending_staging` (no longer a single container)
- [x] 2.3 `ExecuteSubmission`: precondition check (throw `std::runtime_error` unless Reset) → empty batch CPU signal → non-empty batch moves staging, records, submits, and sets state to `Submitted` (transition after successful submit2)
- [x] 2.4 `ExecuteSubmissionImmediately`: precondition check (throw `std::runtime_error` unless Reset) → takes only `m_pending_staging`, submits, waits, clears only its own batch → state stays `Reset`
- [x] 2.5 `OnFrameComplete`: `Submitted` → wait fence, reclaim CB, clear `m_inflight_staging`, set `Reset`; `Reset` with pending non-empty → throw `std::runtime_error`; `Reset` with empty → idempotent return
- [x] 2.6 Destructor defense: `waitForFences` before destruction when `Submitted`
- [x] 2.7 Remove all access to `m_system.GetFrameManager()`; route all device access through `DeviceInterface &` / `AllocatorState &`

## 3. FrameManager Adaptation

- [x] 3.1 Change `impl::Create` to `std::make_unique<SubmissionHelper>(m_system.GetDeviceInterface(), m_system.GetAllocatorState())`
- [x] 3.2 Change the `SubmitFrame` call site from `OnPreMainCbSubmission()` to `ExecuteSubmission(this_timeline_semaphore.GetSignalInfo(2))`, keeping the "Staged resource submission (signals timeline timepoint 2)" comment

## 4. Unit Tests

- [x] 4.1 Create `test/submission_helper_test.cpp`: construct `SubmissionHelper` directly from a headless `GpuContext` (no FrameManager), creating a timeline semaphore for the signal parameter
- [x] 4.2 Cover: empty-batch `ExecuteSubmission` CPU signal advances the value; consecutive `ExecuteSubmission` throws; `ExecuteSubmissionImmediately` while `Submitted` throws; `OnFrameComplete` with unsubmitted Enqueues throws; normal submit → reap → re-submit cycle; consecutive `ExecuteSubmissionImmediately` is legal; immediate/deferred mixing does not corrupt staging; destruction with a pending batch does not crash
- [x] 4.3 Register `submission_helper_test` in `test/CMakeLists.txt` (add_executable + add_test) and get it compiling

## 5. Regression Verification

- [x] 5.1 Build passes (`cmake --preset debug` + `cmake --build --preset debug`)
- [x] 5.2 `ctest --preset debug` passes fully (registered test/ tests + engine/Tests)
- [x] 5.3 Run a windowed example (e.g. physics_example) to verify the render frame loop and physics submission channel have no regression
