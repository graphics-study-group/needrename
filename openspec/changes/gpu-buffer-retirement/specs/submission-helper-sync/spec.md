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

Staging allocated for enqueued operations SHALL be retained by `SubmissionHelper` until the batch that carries it is proven complete, and SHALL be released through the device retirement facility rather than freed directly. `SubmissionHelper` SHALL report the epoch associated with a submission complete before releasing that submission's staging.

#### Scenario: Immediate submission releases staging within its own cycle

- **WHEN** `ExecuteSubmissionImmediately` submits, waits on its fence, and reports its epoch complete
- **THEN** the batch's staging allocations are released without waiting for a later epoch

#### Scenario: Deferred batch staging survives until reap

- **WHEN** a deferred batch is submitted and its staging is still awaiting `OnFrameComplete`
- **THEN** that staging is retained, and is released only after the batch fence has been waited on and the batch's epoch reported complete
