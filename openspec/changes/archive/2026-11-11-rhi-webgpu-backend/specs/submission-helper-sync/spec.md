## MODIFIED Requirements

### Requirement: Parameterized submission signal

`ExecuteSubmission` SHALL accept a completion-signal parameter of the backend-neutral signal kind as the batch completion signal; the parameter MUST NOT name a backend type. When the batch is empty it SHALL advance that signal CPU-side directly. When the batch is non-empty it SHALL carry the signal with the submission of the one-time command buffer. The frame path SHALL pass its own frame signal to `ExecuteSubmission` directly.

#### Scenario: Empty batch CPU signal

- **WHEN** `ExecuteSubmission(signal)` is called without any pending Enqueue operations
- **THEN** no command buffer is submitted, the signal is advanced CPU-side, and the state stays Reset

#### Scenario: Non-empty batch signals with submission

- **WHEN** `ExecuteSubmission(signal)` is called with pending Enqueue operations
- **THEN** a one-time command buffer is recorded and submitted, and the submitted signal carries the caller's completion signal

#### Scenario: Main batch dependency preserved

- **WHEN** a frame has no upload operations and the frame path calls `ExecuteSubmission(its signal)` before the main batch depends on that signal
- **THEN** the signal has already been advanced by the CPU and the main batch does not block

### Requirement: Frame-end reap protocol

`OnFrameComplete` SHALL, in the `Submitted` state, observe the batch's completion, release staging, reclaim the command buffer, and set the state to `Reset`. Observing completion SHALL NOT block a thread that must keep rendering: on a backend whose completion is reported asynchronously, reap SHALL act on the completion record rather than waiting. In the `Reset` state: it SHALL throw `std::runtime_error` if unsubmitted Enqueue operations are pending, and SHALL return idempotently if completely idle.

#### Scenario: Frame end rejected with unsubmitted operations

- **WHEN** `OnFrameComplete` is called after Enqueue operations that were never submitted
- **THEN** it throws `std::runtime_error`

#### Scenario: Idle frame reap is idempotent

- **WHEN** `OnFrameComplete` is called with no Enqueue and no submission
- **THEN** it returns idempotently, the state stays Reset, and no exception is thrown

#### Scenario: Normal reap

- **WHEN** `OnFrameComplete` is called after a submitted `ExecuteSubmission`
- **THEN** the batch's completion is observed, staging and the command buffer are reclaimed, and the state is set to Reset

#### Scenario: Reap does not block an unfinished batch

- **WHEN** `OnFrameComplete` is called while the submitted batch has not yet completed
- **THEN** it does not wait; the batch stays in `Submitted` and its staging is retained until a later reap observes completion
