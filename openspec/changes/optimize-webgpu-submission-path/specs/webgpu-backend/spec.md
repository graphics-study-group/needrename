## MODIFIED Requirements

### Requirement: Dispatches run in their own compute scope and marks are no-ops

Each dispatch SHALL be recorded into a compute scope derived internally by the backend. Consecutive compute commands SHALL share one scope until a command or lifecycle boundary requires that scope to close. A synchronization mark SHALL record nothing: the backend's ordered command execution SHALL preserve the visibility declared by the mark without requiring a scope boundary at that point.

The backend SHALL close the current compute scope before a copy, clear, render scope, command-buffer completion, or encoder destruction. It SHALL NOT expose compute-scope begin/end operations to callers.

#### Scenario: Marked dispatches are ordered

- **WHEN** a recording contains dispatch, mark, dispatch
- **THEN** the second dispatch observes the first dispatch's writes and the mark records no standalone command

#### Scenario: Unmarked dispatches are still ordered

- **WHEN** a recording contains multiple compute dispatches with no mark or other intervening non-compute command
- **THEN** they execute in recording order within one compute scope and each observes the writes required from earlier commands

#### Scenario: Synchronization cost matches the caller's barrier count

- **WHEN** a recording contains N consecutive dispatches and M synchronization marks with no non-compute command between them
- **THEN** the backend uses one shared compute scope rather than N scopes and records no standalone commands for the M marks

#### Scenario: Non-compute command closes the scope

- **WHEN** a recording contains dispatch, a buffer copy or clear, then another dispatch
- **THEN** the first compute scope closes before the copy or clear and the later dispatch observes the completed intermediate operation

#### Scenario: Command-buffer completion closes the scope

- **WHEN** a command buffer is finished while a compute scope is still open
- **THEN** the backend closes the scope before finishing the command buffer and produces a valid submission

### Requirement: Frame completion advances through callbacks

Submission SHALL return without waiting. Completion SHALL be observed through callbacks that advance the epoch watermark used for resource retirement. A frame slot whose previous work is not yet complete SHALL be skipped rather than waited on. Browser product paths MUST NOT block on device completion.

An explicit synchronous fence wait on the native desktop backend MAY block through the platform's wait primitive. It SHALL preserve the caller's timeout and completion semantics and SHALL NOT emulate blocking with repeated short sleeps. The browser backend SHALL continue to deliver completion through its event pump.

#### Scenario: Retirement advances from callbacks

- **WHEN** a submission completes
- **THEN** its completion callback advances the completed prefix and retired resources become reclaimable

#### Scenario: Busy frame slot is skipped

- **WHEN** the frame loop finds its next slot still busy
- **THEN** the frame is skipped and retried on the next tick instead of blocking

#### Scenario: Native fence wait completes without polling sleeps

- **WHEN** native code explicitly waits for submitted work with a finite or infinite timeout
- **THEN** the backend uses the platform blocking-wait facility and returns the same completion or timeout result without a short-sleep polling loop

#### Scenario: Browser completion remains non-blocking

- **WHEN** browser code processes an outstanding completion
- **THEN** it continues through the event pump and does not invoke a native blocking wait

### Requirement: Presentation uses the host surface

The backend SHALL present through the host's surface abstraction, taking one surface image per frame and presenting it at the end of the frame. The CPU-side frame ring and its per-frame windows SHALL remain valid regardless of how many surface images the host exposes.

Native presentation SHALL select Mailbox when the surface supports it and SHALL fall back to FIFO otherwise. Browser presentation SHALL use FIFO. Immediate mode SHALL NOT be selected as the default on either target.

#### Scenario: Frame renders to the surface

- **WHEN** a frame is recorded and presented in the browser build
- **THEN** the rendered image reaches the canvas

#### Scenario: Native surface supports Mailbox

- **WHEN** a native surface offers both Mailbox and FIFO
- **THEN** the backend selects Mailbox

#### Scenario: Native surface lacks Mailbox

- **WHEN** a native surface offers FIFO but not Mailbox
- **THEN** the backend configures FIFO and presentation remains valid

#### Scenario: Frame ring is independent of surface image count

- **WHEN** the host exposes one surface image while the frame ring has three slots
- **THEN** per-frame windows stay distinct and no slot's data is overwritten while in flight

## ADDED Requirements

### Requirement: Per-command constants are published in batches

The backend SHALL accumulate constant windows recorded into one command buffer and SHALL publish their contiguous used range with at most one queue write immediately before that command buffer is submitted. Commands recorded into different command buffers SHALL retain independent host staging even when their device-side window offsets are identical.

Publication SHALL follow queue order relative to other submissions. Recording command buffers in an interleaved order MUST NOT allow one submission's constants to overwrite values still needed by an earlier submission. Unused bytes in each fixed-size constant window SHALL be zero, and a recording that exceeds the available ring capacity SHALL report an error rather than silently wrapping or splitting publication.

#### Scenario: One submission publishes its constants once

- **WHEN** a command buffer records multiple commands carrying constant blocks and is submitted
- **THEN** the backend publishes all of those windows with one contiguous queue write before the submission

#### Scenario: Interleaved recording preserves submission ordering

- **WHEN** command buffers A and B are recorded with different constants and then submitted in the order A, B
- **THEN** A observes A's values and B observes B's values despite any reuse of device-side window offsets

#### Scenario: Short constant block has deterministic padding

- **WHEN** a command records fewer bytes than its declared constant window
- **THEN** the shader observes the recorded prefix and zeroes in the remainder of the window

#### Scenario: Constant ring capacity is exceeded

- **WHEN** one command buffer records more constant windows than the ring can hold
- **THEN** recording reports an explicit error and does not wrap onto an earlier window

### Requirement: Dispatch validation remains active in every build configuration

The backend SHALL validate named interfaces, supplied resource types, and writable storage-buffer aliasing while recording dispatches in both Release and Debug configurations. Invalid input SHALL produce a diagnostic that identifies the kernel and offending interface or buffers. Performance work MUST NOT disable these checks or defer them solely to the underlying WebGPU implementation.

#### Scenario: Writable storage aliasing is rejected

- **WHEN** one dispatch binds the same buffer through multiple storage interfaces and at least one interface is writable
- **THEN** recording reports an error naming the kernel and conflicting bindings before issuing the dispatch

#### Scenario: Resource type mismatch is rejected

- **WHEN** a dispatch supplies a buffer for an interface that requires a texture or a texture for an interface that requires a buffer
- **THEN** recording reports an error naming the kernel and interface

#### Scenario: Valid dispatch retains identical behavior across configurations

- **WHEN** the same valid dispatch is recorded in Release and Debug builds
- **THEN** both produce the same bound resources, constants, and dispatch grid
