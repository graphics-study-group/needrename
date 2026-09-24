# submission-helper-sync

## MODIFIED Requirements

### Requirement: Independent dependency construction

`SubmissionHelper` SHALL reside in the `Rhi` module as `Engine::Rhi::SubmissionHelper` (moved from `Render/RenderSystem/` with namespace `Engine::RenderSystemState`). It SHALL be constructible from Rhi-provided facilities only — the `DeviceInterface &`, the `AllocatorState &`, and the device-scoped submission epoch tracker — and MUST NOT access or reference `RenderSystem` / `FrameManager`. All device access (device, queue info, staging allocation) MUST go through those parameters.

#### Scenario: Standalone construction without FrameManager

- **WHEN** constructing `SubmissionHelper` directly with the `GetDeviceInterface()` and `GetAllocatorState()` of a headless `Rhi` setup, together with a retirement facility built from those same facilities
- **THEN** construction succeeds, and `Rhi/SubmissionHelper.h` does not depend on `Render/RenderSystem.h` or `FrameSemaphore.hpp`

#### Scenario: FrameManager construction adaptation

- **WHEN** `FrameManager::impl::Create` creates the `SubmissionHelper`
- **THEN** it passes `m_system.GetDeviceInterface()` and `m_system.GetAllocatorState()`, with behavior equivalent to before the refactor

### Requirement: Single-thread usage constraint

The public methods of `SubmissionHelper` (Enqueue, submit, reap) MUST be called from a single thread (the frame loop thread). The header documentation SHALL declare this constraint, and the implementation SHALL NOT introduce thread synchronization primitives.

Recording and reporting submission epochs is a completion ledger, not a thread synchronization primitive, and SHALL NOT be read as violating this constraint.

#### Scenario: Constraint declared

- **WHEN** inspecting the class-level documentation in `SubmissionHelper.h`
- **THEN** it declares the single-thread usage constraint and the implementation performs no thread synchronization

#### Scenario: Epoch participation is not thread synchronization

- **WHEN** `SubmissionHelper` opens an epoch, submits, waits on its fence, and reports the epoch complete
- **THEN** no mutex, atomic, condition variable, or thread-local state is introduced by that participation

## ADDED Requirements

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
