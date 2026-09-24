# Submission Helper Sync

## Purpose

Defines `SubmissionHelper` as a render-system-independent batch submission component: it is constructed from only a `DeviceInterface &` and an `AllocatorState &`, is driven by a caller-provided submission signal, and enforces an explicit batch state machine with per-batch staging lifecycle and destructor safety. Usage is restricted to a single thread; no synchronization primitives are introduced.

## Requirements

### Requirement: Independent dependency construction

`SubmissionHelper` SHALL reside in the `Rhi` module as `Engine::Rhi::SubmissionHelper` (moved from `Render/RenderSystem/` with namespace `Engine::RenderSystemState`). It SHALL be constructible from Rhi-provided facilities only — the `DeviceInterface &`, the `AllocatorState &`, and the device-scoped submission epoch tracker — and MUST NOT access or reference `RenderSystem` / `FrameManager`. All device access (device, queue info, staging allocation) MUST go through those parameters.

#### Scenario: Standalone construction without FrameManager

- **WHEN** constructing `SubmissionHelper` directly with the `GetDeviceInterface()` and `GetAllocatorState()` of a headless `Rhi` setup, together with a retirement facility built from those same facilities
- **THEN** construction succeeds, and `Rhi/SubmissionHelper.h` does not depend on `Render/RenderSystem.h` or `FrameSemaphore.hpp`

#### Scenario: FrameManager construction adaptation

- **WHEN** `FrameManager::impl::Create` creates the `SubmissionHelper`
- **THEN** it passes `m_system.GetDeviceInterface()` and `m_system.GetAllocatorState()`, with behavior equivalent to before the refactor

### Requirement: Parameterized submission signal

`ExecuteSubmission` SHALL accept a `vk::SemaphoreSignalInfo` parameter as the batch completion signal. When the batch is empty it SHALL advance that signal CPU-side via `signalSemaphore` directly. When the batch is non-empty it SHALL signal the semaphore with the submission of the one-time command buffer (signal stage `eAllTransfer`). `OnPreMainCbSubmission` SHALL be removed, and `FrameManager::SubmitFrame` SHALL call `ExecuteSubmission(this_timeline_semaphore.GetSignalInfo(2))` directly.

#### Scenario: Empty batch CPU signal

- **WHEN** `ExecuteSubmission(signal)` is called without any pending Enqueue operations
- **THEN** no command buffer is submitted, the signal is advanced CPU-side, and the state stays Reset

#### Scenario: Non-empty batch signals with submission

- **WHEN** `ExecuteSubmission(signal)` is called with pending Enqueue operations
- **THEN** a one-time command buffer is recorded and submitted, and the submitted signal carries the caller's semaphore and value

#### Scenario: Main batch dependency preserved

- **WHEN** a frame has no upload operations and `FrameManager::SubmitFrame` calls `ExecuteSubmission(GetSignalInfo(2))` before the main batch waits on timepoint 2
- **THEN** timepoint 2 has already been advanced by the CPU signal and the main batch does not block

### Requirement: Batch state machine protocol

`SubmissionHelper` SHALL maintain a batch state machine with `Reset` (submittable) and `Submitted` (deferred batch pending, awaiting reap). Calling `ExecuteSubmission` while `Submitted` SHALL throw `std::runtime_error`. Calling `ExecuteSubmissionImmediately` while `Submitted` SHALL throw `std::runtime_error`. A successful `ExecuteSubmissionImmediately` SHALL leave the state at `Reset` (self-contained).

#### Scenario: Consecutive deferred submissions rejected

- **WHEN** `ExecuteSubmission` is called, then called again before any reap
- **THEN** the second call throws `std::runtime_error` without any resource side effects

#### Scenario: Immediate submission while pending rejected

- **WHEN** `ExecuteSubmissionImmediately` is called while a deferred batch is pending (state Submitted)
- **THEN** it throws `std::runtime_error`

#### Scenario: Consecutive immediate submissions are legal

- **WHEN** `ExecuteSubmissionImmediately` is called repeatedly (each time after an Enqueue)
- **THEN** every call succeeds without throwing

#### Scenario: Re-submission after reap

- **WHEN** `ExecuteSubmission` is followed by `OnFrameComplete` (reap), then `ExecuteSubmission` is called again
- **THEN** the second submission succeeds and the state machine flows normally

### Requirement: Frame-end reap protocol

`OnFrameComplete` SHALL, in the `Submitted` state, wait on the batch fence, release staging, reclaim the command buffer, and set the state to `Reset`. In the `Reset` state: it SHALL throw `std::runtime_error` if unsubmitted Enqueue operations are pending, and SHALL return idempotently if completely idle.

#### Scenario: Frame end rejected with unsubmitted operations

- **WHEN** `OnFrameComplete` is called after Enqueue operations that were never submitted
- **THEN** it throws `std::runtime_error`

#### Scenario: Idle frame reap is idempotent

- **WHEN** `OnFrameComplete` is called with no Enqueue and no submission
- **THEN** it returns idempotently, the state stays Reset, and no exception is thrown

#### Scenario: Normal reap

- **WHEN** `OnFrameComplete` is called after a submitted `ExecuteSubmission`
- **THEN** the batch fence is waited on, staging and the command buffer are reclaimed, and the state is set to Reset

### Requirement: Per-batch staging lifecycle

`SubmissionHelper` SHALL account for staging per batch: staging not yet submitted and staging submitted but awaiting reap SHALL be held in separate containers. `ExecuteSubmissionImmediately` SHALL submit and reap only the operations pending at the time of the call together with their staging, and MUST NOT release staging of a deferred batch that was submitted earlier and is still awaiting reap.

#### Scenario: Mixing immediate and deferred submissions does not corrupt staging

- **WHEN** a deferred batch is submitted (staging awaiting reap) and `ExecuteSubmissionImmediately` then runs (with its own Enqueued operations), in a state where the protocol allows it
- **THEN** the immediate submission reaps only its own batch's staging, and the deferred batch's staging is preserved until its `OnFrameComplete` reap

#### Scenario: Immediate submission semantics

- **WHEN** pending Enqueue operations are executed via `ExecuteSubmissionImmediately`
- **THEN** all currently pending operations are submitted and waited on to completion, after which normal usage can continue

### Requirement: Destructor safety

`SubmissionHelper`'s destructor SHALL wait on the batch fence before destroying resources when a deferred batch is still pending (`Submitted`).

#### Scenario: Destruction with a pending batch

- **WHEN** the `SubmissionHelper` is destroyed while a deferred batch is submitted but not yet reaped
- **THEN** the destructor waits for the batch to finish before destroying, leaving no dangling GPU resources

### Requirement: Single-thread usage constraint

The public methods of `SubmissionHelper` (Enqueue, submit, reap) MUST be called from a single thread (the frame loop thread). The header documentation SHALL declare this constraint, and the implementation SHALL NOT introduce thread synchronization primitives.

Recording and reporting submission epochs is a completion ledger, not a thread synchronization primitive, and SHALL NOT be read as violating this constraint.

#### Scenario: Constraint declared

- **WHEN** inspecting the class-level documentation in `SubmissionHelper.h`
- **THEN** it declares the single-thread usage constraint and the implementation performs no thread synchronization

#### Scenario: Epoch participation is not thread synchronization

- **WHEN** `SubmissionHelper` opens an epoch, submits, waits on its fence, and reports the epoch complete
- **THEN** no mutex, atomic, condition variable, or thread-local state is introduced by that participation

### Requirement: Staging release participates in retirement

Staging allocated for enqueued operations SHALL be retained by `SubmissionHelper` until the batch that carries it is proven complete, and SHALL be released through the device retirement facility rather than freed directly.

`SubmissionHelper` SHALL open **its own** epoch for each submission it issues, and SHALL report that epoch after waiting the fence of that submission. It MUST NOT report an epoch it did not open — in particular it MUST NOT report the frame's epoch when it reaps a batch, because that batch's fence says nothing about the frame's main batch, which is still in flight.

Staging SHALL be released under the retirement facility's exact mode, naming the epoch of the submission that carried it, so that its release condition is already satisfied at the moment of release.

#### Scenario: Immediate submission releases staging within its own cycle

- **WHEN** `ExecuteSubmissionImmediately` opens its own epoch, submits, waits on its fence, reports that epoch, and releases the batch's staging under the exact mode
- **THEN** the staging allocations are released immediately, without waiting for the completed prefix

#### Scenario: Deferred batch staging is released at its reap, not a frame later

- **WHEN** a deferred batch is submitted and its staging is still awaiting `OnFrameComplete`
- **THEN** that staging is retained until the batch fence has been waited on
- **AND** it is released as soon as that batch's own epoch is reported
- **AND** it is not retained until the completed prefix reaches that epoch

#### Scenario: Reaping a batch does not report the frame's epoch

- **WHEN** `OnFrameComplete` reaps a deferred batch whose upload was submitted inside a frame epoch opened by another submitter
- **THEN** it reports only the epoch it opened itself for that upload submission
- **AND** the frame's epoch remains outstanding until its own submitter observes the frame's completion signal
