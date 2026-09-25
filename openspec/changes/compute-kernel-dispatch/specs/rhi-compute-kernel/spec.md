# rhi-compute-kernel

## Purpose

Defines the single call surface for invoking a compute shader: a kernel is identified by its SPIR-V module, invoked with a name-to-resource dictionary plus a dispatch grid, validated against the shader's reflected interface, and recorded without any implicit synchronization.

## ADDED Requirements

### Requirement: A kernel is identified by its SPIR-V module

A compute kernel SHALL be identified by the SPIR-V module it was created from. Requesting a kernel for the same SPIR-V module more than once on the same device SHALL yield the same kernel, and the device SHALL hold exactly one compute pipeline per distinct SPIR-V module regardless of how many components dispatch it.

Kernel creation SHALL be lazy: no pipeline, descriptor set layout, or pipeline layout SHALL be created for a module before its first request.

#### Scenario: Two components share one shader

- **WHEN** two independent components each request the kernel for the same SPIR-V module
- **THEN** both receive the same kernel
- **AND** exactly one compute pipeline exists for that module on the device

#### Scenario: Distinct modules yield distinct kernels

- **WHEN** kernels are requested for two different SPIR-V modules
- **THEN** each request yields a kernel bound to its own module and pipeline

#### Scenario: Nothing is created before first use

- **WHEN** a program creates a device context and never requests a kernel
- **THEN** no compute pipeline, descriptor set layout, or descriptor pool is created

### Requirement: A kernel has no Asset dependency

A kernel SHALL be constructible from Rhi facilities and a SPIR-V word sequence. It MUST NOT depend on the Asset module, and MUST NOT accept an asset-typed creation overload.

#### Scenario: Kernel created from SPIR-V words

- **WHEN** a kernel is created from a `std::vector<uint32_t>` of SPIR-V words and a name
- **THEN** a compute pipeline, pipeline layout and descriptor set layout are created on the device
- **AND** no Asset type is referenced by the kernel's interface

### Requirement: Dispatch takes a name-to-resource dictionary and a grid

Invoking a kernel SHALL be a single call taking the command buffer, the resources keyed by the shader's declared interface names, the workgroup counts for all three dimensions, and the push-constant value. A caller MUST NOT be required to allocate a binding object, select a rotation slot, or issue separate pipeline-bind, resource-bind and dispatch calls.

#### Scenario: One call records one dispatch

- **WHEN** a caller dispatches a kernel with its resource dictionary, a grid and a push-constant value
- **THEN** the pipeline is bound, the resources are bound, the push constant is recorded, and the dispatch is recorded
- **AND** no additional binding or dispatch call is required from the caller

#### Scenario: Repeated dispatch of one kernel

- **WHEN** the same kernel is dispatched several times in one command buffer with different resources
- **THEN** each dispatch records its own resource binding
- **AND** the caller does not create any binding object between dispatches

### Requirement: Each dispatch acquires its descriptor set for the current epoch

A dispatch SHALL obtain its descriptor set from the device's descriptor arena at the moment it is recorded, and a kernel SHALL NOT retain a descriptor-set handle between dispatches.

Re-acquiring on every dispatch is what keeps the arena's record of the acquiring epoch accurate, and therefore what makes the arena's reuse cache safe to reclaim from. A kernel that held a handle between dispatches would keep binding a set the arena may already have released.

The caller MUST NOT supply a rotation slot or a binding object: the arena reclaims a set only once the completed prefix has passed the epoch recorded for it, and that record stays accurate because every dispatch refreshes it.

#### Scenario: Repeated dispatches re-acquire and reuse

- **WHEN** the same kernel is dispatched twice within one epoch with identical resources, and again in the next epoch
- **THEN** each dispatch acquires its set from the arena
- **AND** all three receive the same set, with no new set created after the first

#### Scenario: A kernel holds no set between dispatches

- **WHEN** a kernel's dispatch returns
- **THEN** the kernel retains no descriptor-set handle
- **AND** nothing in the kernel needs to be released when the kernel is destroyed

### Requirement: Interface names are resolved from reflection, once per kernel

The mapping from an interface name to its descriptor set and binding SHALL be derived from the shader module's reflected decorations. The C++ side MUST NOT declare descriptor set or binding numbers.

Each declared interface name SHALL be resolved to a dense table when the kernel is created. Dispatch-time work SHALL scale with the number of bound resources and MUST NOT hash or compare interface name strings.

#### Scenario: A shader with arbitrary binding numbers needs no C++ change

- **WHEN** a shader declares its interfaces at any set and binding numbers
- **THEN** the caller binds them by the names declared in the shader
- **AND** no C++ source states a descriptor set or binding number for that shader

#### Scenario: Renaming a declared interface is a one-place change

- **WHEN** a shader's block name is changed and the kernel is re-requested for the new module
- **THEN** only the dictionary key at the dispatch site changes
- **AND** no descriptor-number bookkeeping changes anywhere

### Requirement: A dictionary entry is a buffer or a texture

Each dictionary entry SHALL name either a buffer — optionally with an offset and size restricting the bound range — or a texture, optionally with a subresource range. A texture entry SHALL be bound for sampling or random access according to the interface type the shader declares.

#### Scenario: Storage buffers bound by name

- **WHEN** a kernel whose shader declares several storage buffers is dispatched
- **THEN** each declared buffer name maps to the buffer supplied for it
- **AND** a supplied offset and size restrict the bound range for that interface

#### Scenario: Textures bound by name

- **WHEN** a kernel whose shader declares sampled and storage images is dispatched
- **THEN** each declared image name maps to the supplied texture
- **AND** the image is bound in the layout the declared interface type requires

### Requirement: Dispatch validates the dictionary and throws on mismatch

Dispatch SHALL throw `std::runtime_error` when the dictionary names an interface the shader does not declare, and when it omits an interface the shader does declare. The diagnostic SHALL identify the shader and the offending interface name.

A declared interface with no bound resource MUST NOT be silently skipped, and MUST NOT leave an unwritten descriptor.

#### Scenario: Undeclared name is rejected

- **WHEN** a dictionary contains a name the shader does not declare
- **THEN** dispatch throws `std::runtime_error`
- **AND** the message names the shader and the offending interface

#### Scenario: Omitted declared interface is rejected

- **WHEN** a dictionary omits an interface the shader declares
- **THEN** dispatch throws `std::runtime_error`
- **AND** the message names the shader and the missing interface

#### Scenario: Complete dictionary dispatches

- **WHEN** a dictionary names exactly the interfaces the shader declares
- **THEN** dispatch records the command without throwing

### Requirement: Push constants are declared from reflection and recorded by dispatch

A kernel SHALL declare a push-constant range covering exactly the reflected push-constant block size, and SHALL declare no push-constant range when the shader declares no push-constant block.

A push-constant value recorded through dispatch SHALL be written at offset 0 with its own size, and debug builds SHALL assert that this size does not exceed the reflected block size.

#### Scenario: Shader with a push block gets a matching range

- **WHEN** a kernel is created from a shader declaring a 16-byte push-constant block
- **THEN** the kernel reports a reflected push-constant size of 16
- **AND** its pipeline layout's push-constant ranges cover `{eCompute, 0, 16}`

#### Scenario: Shader without a push block gets no range

- **WHEN** a kernel is created from a shader declaring no push-constant block
- **THEN** the kernel reports a reflected push-constant size of 0
- **AND** its pipeline layout contains no push-constant range

#### Scenario: Oversized push value is caught in debug builds

- **WHEN** a caller records a push-constant value larger than the reflected block size
- **THEN** the dispatch asserts in debug builds

#### Scenario: Recorded push value reaches the shader

- **WHEN** a caller dispatches with a push-constant value of type `T`
- **THEN** the command buffer receives `sizeof(T)` bytes of that value at offset 0

### Requirement: The kernel inserts no barrier

Dispatch MUST NOT record any barrier, and MUST NOT infer one from the resources in its dictionary. Synchronization between dispatches is the caller's responsibility.

#### Scenario: Consecutive dispatches record no barrier

- **WHEN** two dispatches of the same kernel follow each other with no explicit barrier from the caller
- **THEN** the command buffer contains no barrier between them

#### Scenario: Caller-inserted barriers are preserved

- **WHEN** a caller records a barrier before a dispatch
- **THEN** that barrier is present in the command buffer ahead of the dispatch
- **AND** the dispatch adds nothing to it

#### Scenario: Barrier ownership boundaries

- **WHEN** synchronization is required inside one algorithm recording call
- **THEN** the algorithm records it
- **WHEN** synchronization is required between two recording calls, or between different kernels
- **THEN** the caller records it

### Requirement: No render-frame vocabulary in the kernel interface

The kernel's public interface and its documentation SHALL contain no render-frame vocabulary: no back-buffer index, no frame index, no frames-in-flight counter, and no rotation slot.

Dispatch MUST NOT accept a rotation, slot, or frame parameter.

#### Scenario: No rotation parameter exists

- **WHEN** the kernel's dispatch entry point is inspected
- **THEN** it takes the command buffer, the resource dictionary, the grid and the push-constant value
- **AND** it takes no slot, rotation, back-buffer or frame parameter

#### Scenario: No presentation vocabulary in the interface

- **WHEN** the kernel facility's public headers and documentation are searched for `backbuffer`, `frame_index`, `frames-in-flight`, and `slot_count`
- **THEN** no match describes a kernel concept

### Requirement: A kernel is immutable once created

A kernel's pipeline, reflected layout and resolved interface table SHALL be immutable after creation, and the kernel SHALL remain valid for the lifetime of the device context it was created from.

Runtime shader reload is out of scope: SPIR-V that changes content MUST be treated as a distinct module identity rather than mutating an existing kernel.

#### Scenario: Kernel outlives its requester

- **WHEN** a component that requested a kernel is destroyed while another component still dispatches it
- **THEN** the kernel remains valid and usable

#### Scenario: Changed SPIR-V is a distinct kernel

- **WHEN** a module's SPIR-V words change and a kernel is requested for them
- **THEN** the request is not satisfied by a kernel created from the previous words

### Requirement: The kernel facility works without a frame loop

The kernel facility SHALL be usable with no presentation frame loop, no `FrameManager` and no render system. Creating a kernel and dispatching it SHALL require only the device facilities.

#### Scenario: Headless kernel dispatch

- **WHEN** a headless program creates a device context, requests a kernel and dispatches it to its own command buffer
- **THEN** the dispatch records and executes successfully
- **AND** no presentation or frame-loop type is required
