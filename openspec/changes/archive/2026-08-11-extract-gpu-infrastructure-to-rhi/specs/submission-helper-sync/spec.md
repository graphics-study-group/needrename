# Submission Helper Sync

## Purpose

Defines `SubmissionHelper` as a render-system-independent batch submission component: it is constructed from only a `DeviceInterface &` and an `AllocatorState &`, is driven by a caller-provided submission signal, and enforces an explicit batch state machine with per-batch staging lifecycle and destructor safety. Usage is restricted to a single thread; no synchronization primitives are introduced.

## MODIFIED Requirements

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
