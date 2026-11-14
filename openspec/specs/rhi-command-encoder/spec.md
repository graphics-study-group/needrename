# rhi-command-encoder Specification

## Purpose

Defines the recording contract every GPU consumer uses: a flat command stream of dispatches, draws and copies in which synchronization is declared explicitly at the places the caller knows about, per-dispatch constants and uniform windows are recording operations, and pass vocabulary does not appear.

## Requirements

### Requirement: Recording is flat and carries no pass vocabulary

A consumer SHALL record dispatches, draws and copies directly into a command encoder without opening or closing a compute scope. The interface MUST NOT expose compute-pass begin/end operations; a backend that needs scopes SHALL derive them internally.

#### Scenario: Multi-dispatch recording stays flat

- **WHEN** a physics algorithm records three dispatches separated by two synchronization marks into one encoder
- **THEN** the consumer issues one dispatch call per dispatch and one mark call per synchronization point, and no scope call

#### Scenario: Backend derives its own scopes

- **WHEN** the WebGPU backend receives such a recording
- **THEN** it opens and closes its own compute scopes internally and the consumer is not asked about them

### Requirement: Synchronization marks are the caller's declared sync points

The encoder SHALL offer a general synchronization mark and a texture-state synchronization mark. Both SHALL mean "the work recorded before this mark is visible to the work recorded after it". Their placement is the caller's knowledge of its own data flow; a backend MUST NOT add, remove or move their semantic synchronization points.

A backend SHALL satisfy each mark through its own ordering mechanism. A mark need not produce a distinct command or compute-scope boundary when the backend already guarantees the required visibility within the current command sequence.

#### Scenario: Vulkan backend records a real barrier

- **WHEN** a caller records a synchronization mark between two dispatches
- **THEN** the Vulkan backend records a pipeline barrier at that point and nothing else between the two dispatches

#### Scenario: WebGPU backend carries ordering by scope boundary

- **WHEN** the WebGPU backend receives a synchronization mark between two dispatches
- **THEN** the mark itself records no command, and the second dispatch observes the first dispatch's writes whether the backend keeps them in one compute scope or separates them

#### Scenario: The kernel adds no synchronization

- **WHEN** two dispatches follow each other with no mark between them
- **THEN** neither backend inserts an explicit barrier or a mandatory scope boundary between them

### Requirement: Synchronization marks are forbidden inside a render scope

A synchronization mark MUST NOT be recorded between the begin and end of a render scope. Recording one there SHALL be rejected (asserted in debug builds and rejected in release builds).

#### Scenario: Mark inside a render scope is rejected

- **WHEN** a caller records a synchronization mark after beginning a render scope and before ending it
- **THEN** the recording is rejected and no command stream is produced from it

### Requirement: Per-dispatch constants are recorded as kernel parameters

The encoder SHALL record a per-dispatch constant block with the semantics of kernel parameters: a small, CPU-supplied value that must reach the shader for exactly that dispatch. The block size contract SHALL come from the shader's reflected constant block, and debug builds SHALL assert that a recorded block does not exceed it. Each backend SHALL map the value to its own mechanism: the Vulkan backend to real push constants when the block fits its device limit, the WebGPU backend to a slice of a per-command constant ring bound as a uniform.

Recording a constant block and publishing its bytes to the device MAY be separate operations. A backend that defers publication SHALL preserve each command's distinct bytes, zero-fill the unused portion of its declared window, and publish the data before the consuming submission.

#### Scenario: Constants reach the shader on both backends

- **WHEN** a caller records a constant block and then a dispatch
- **THEN** the dispatched shader observes exactly those bytes as its constant block, on both backends

#### Scenario: Oversized constant block is caught

- **WHEN** a caller records a constant block larger than the reflected block size
- **THEN** the recording asserts in debug builds

#### Scenario: Backend maps constants to its own mechanism

- **WHEN** the WebGPU backend records a dispatch that carries a constant block
- **THEN** the value is carried by a uniform-buffer slice rather than by any backend-specific immediate-data feature

#### Scenario: Deferred publication preserves every command's value

- **WHEN** one command buffer records multiple dispatches or draws with different constant blocks and is then submitted
- **THEN** every command observes its own recorded bytes and no command observes a later command's values

### Requirement: Uniform windows are recorded as resource slices

The encoder SHALL record "bind this resource's byte range as the uniform or storage window for the following draws or dispatches", carrying the resource, the offset and the size. The Vulkan backend SHALL map this to its native dynamic-offset or offset mechanism; the WebGPU backend SHALL map it to a binding cached per distinct window content.

#### Scenario: Slice semantics are identical across backends

- **WHEN** a caller records a window over a buffer and then a draw
- **THEN** the shader sees exactly that byte range as the bound window, on both backends

#### Scenario: Distinct windows do not alias

- **WHEN** a caller records two different windows over the same buffer for two dispatches
- **THEN** each dispatch sees its own window and neither observes the other's

### Requirement: The render scope is the only pass-shaped construct in the interface

The interface SHALL expose exactly one scope: the render scope that begins and ends a rendering region over a set of attachments. Draws SHALL be legal only inside it; dispatches, copies and synchronization marks SHALL be legal only outside it.

#### Scenario: Draws are confined to the render scope

- **WHEN** a caller records a draw outside a render scope
- **THEN** the recording is rejected

#### Scenario: Dispatch is confined outside the render scope

- **WHEN** a caller records a dispatch inside a render scope
- **THEN** the recording is rejected

### Requirement: Copies are ordered with the scopes around them

Copy operations SHALL be recordable anywhere in the flat stream and SHALL be ordered with respect to the scopes and dispatches recorded before and after them.

#### Scenario: Copy between two dispatches is ordered

- **WHEN** a caller records dispatch A, then a buffer copy, then dispatch B
- **THEN** the copy observes A's writes and B observes the copy's writes, on both backends
