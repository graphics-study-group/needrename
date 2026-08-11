# Submission Helper Sync

## Purpose

Defines `SubmissionHelper` as a render-system-independent batch submission component: it is constructed from only a `DeviceInterface &` and an `AllocatorState &`, is driven by a caller-provided submission signal, and enforces an explicit batch state machine with per-batch staging lifecycle and destructor safety. Usage is restricted to a single thread; no synchronization primitives are introduced.

## Requirements

### Requirement: Independent dependency construction

`SubmissionHelper` SHALL reside in the `Rhi` module as `Engine::Rhi::SubmissionHelper` (moved from `Render/RenderSystem/` with namespace `Engine::RenderSystemState`). It SHALL be constructible with only a `DeviceInterface &` and an `AllocatorState &`, and MUST NOT access or reference `RenderSystem` / `FrameManager`. All device access (device, queue info, staging allocation) MUST go through these two parameters.

#### Scenario: Standalone construction without FrameManager

- **WHEN** constructing `SubmissionHelper` directly with the `GetDeviceInterface()` and `GetAllocatorState()` of a headless `Rhi` setup
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

The public methods of `SubmissionHelper` (Enqueue, submit, reap) MUST be called from a single thread (the frame loop thread). The header documentation SHALL declare this constraint, and the implementation SHALL NOT introduce synchronization primitives.

#### Scenario: Constraint declared

- **WHEN** inspecting the class-level documentation in `SubmissionHelper.h`
- **THEN** it declares the single-thread usage constraint and the implementation performs no thread synchronization
