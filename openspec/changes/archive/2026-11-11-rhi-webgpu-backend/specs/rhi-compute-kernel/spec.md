## MODIFIED Requirements

### Requirement: Dispatch takes a name-to-resource dictionary and a grid

Invoking a kernel SHALL be a single call taking the command encoder, the resources keyed by the shader's declared interface names, the workgroup counts for all three dimensions, and the per-dispatch constant block. A caller MUST NOT be required to allocate a binding object, select a rotation slot, or issue separate pipeline-bind, resource-bind and dispatch calls.

#### Scenario: One call records one dispatch

- **WHEN** a caller dispatches a kernel with its resource dictionary, a grid and a constant block
- **THEN** the pipeline is bound, the resources are bound, the constant block is recorded, and the dispatch is recorded
- **AND** no additional binding or dispatch call is required from the caller

#### Scenario: Repeated dispatch of one kernel

- **WHEN** the same kernel is dispatched several times in one command encoder with different resources
- **THEN** each dispatch records its own resource binding
- **AND** the caller does not create any binding object between dispatches

### Requirement: Per-dispatch constants are declared from reflection and recorded by dispatch

A kernel SHALL declare the size of its per-dispatch constant block from exactly the reflected constant-block size of the shader, and SHALL declare a size of zero when the shader declares no constant block. The block is the kernel-parameter contract: the caller supplies the value, the backend maps it to its own mechanism.

A constant block recorded through dispatch SHALL reach the shader as the whole block at offset 0 with its own size, and debug builds SHALL assert that this size does not exceed the reflected block size. The recording mechanism is the backend's choice: a backend with native per-dispatch constants SHALL use them while the block fits its device limit, and a backend without them SHALL carry the block in a uniform-buffer window bound for that dispatch.

#### Scenario: Shader with a constant block reports its size

- **WHEN** a kernel is created from a shader declaring a 16-byte constant block
- **THEN** the kernel reports a reflected constant-block size of 16

#### Scenario: Shader without a constant block reports zero

- **WHEN** a kernel is created from a shader declaring no constant block
- **THEN** the kernel reports a reflected constant-block size of 0
- **AND** dispatching it with an empty constant block succeeds

#### Scenario: Oversized constant block is caught in debug builds

- **WHEN** a caller records a constant block larger than the reflected block size
- **THEN** the dispatch asserts in debug builds

#### Scenario: Recorded value reaches the shader

- **WHEN** a caller dispatches with a constant block of type `T`
- **THEN** the dispatched shader observes `sizeof(T)` bytes of that value as its constant block

## RENAMED Requirements

FROM: `Push constants are declared from reflection and recorded by dispatch`
TO: `Per-dispatch constants are declared from reflection and recorded by dispatch`
