# rhi-compute-kernel

## Purpose

Defines the single call surface for invoking a compute shader: a kernel is identified by a caller-supplied module identity, invoked with a name-to-resource dictionary plus a dispatch grid, validated against the shader's reflected interface, and recorded without any implicit synchronization.

## ADDED Requirements

### Requirement: A kernel is identified by a caller-supplied module identity

A compute kernel SHALL be identified by a module identity string supplied alongside its SPIR-V words. Requesting a kernel for an identity the device already knows SHALL return the existing kernel without reading, hashing or retaining the module's words, and the device SHALL hold exactly one compute pipeline per identity regardless of how many components dispatch it.

The identity is the caller's assertion that two requests bearing the same identity denote the same module. The facility MUST NOT verify it against the module's contents, and MUST NOT require any particular form of it. Identities come from wherever a shader is already managed: the engine's shader assets use their asset GUID, and directly file-loaded SPIR-V uses its path.

Kernel creation SHALL be lazy: no pipeline, descriptor set layout, or pipeline layout SHALL be created for an identity before its first request.

#### Scenario: Two components share one shader

- **WHEN** two independent components each request the kernel for the same module identity
- **THEN** both receive the same kernel
- **AND** exactly one compute pipeline exists for that identity on the device
- **AND** the second request needs no SPIR-V words

#### Scenario: A known identity is found without its words

- **WHEN** a caller probes a module identity the device already knows
- **THEN** the kernel is returned without the module being read, hashed or compared
- **AND** probing an unknown identity yields no kernel and creates nothing

#### Scenario: Distinct identities yield distinct kernels

- **WHEN** kernels are requested for two different module identities
- **THEN** each request yields a kernel bound to its own module and pipeline

#### Scenario: Nothing is created before first use

- **WHEN** a program creates a device context and never requests a kernel
- **THEN** no compute pipeline, descriptor set layout, or descriptor pool is created

### Requirement: A kernel has no Asset dependency

A kernel SHALL be constructible from Rhi facilities, a SPIR-V word sequence and a module identity. It MUST NOT depend on the Asset module, and MUST NOT accept an asset-typed creation overload.

#### Scenario: Kernel created from SPIR-V words

- **WHEN** a kernel is created from a `std::vector<uint32_t>` of SPIR-V words, a module identity and a name
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

A kernel SHALL obtain its descriptor-set layout from the arena once, at creation, build its pipeline layout over that resolved object, and pass that same object to the arena's cache layer on every dispatch, together with the dynamic-offset flags its layout description was created with and the set index it resolved. A kernel MUST NOT resolve a layout description of its own, and MUST NOT hand the arena a layout the arena did not resolve.

The resolved binding entries a dispatch hands the arena SHALL be ordered canonically by ascending binding number, so that equal resources presented in any dictionary order key to one arena entry instead of minting a set per order.

Re-acquiring on every dispatch is what keeps the arena's record of the acquiring epoch accurate, and therefore what makes the arena's reuse cache safe to reclaim from. A kernel that held a handle between dispatches would keep binding a set the arena may already have released.

The caller MUST NOT supply a rotation slot or a binding object: the arena reclaims a set only once the completed prefix has passed the epoch recorded for it, and that record stays accurate because every dispatch refreshes it.

#### Scenario: Repeated dispatches re-acquire and reuse

- **WHEN** the same kernel is dispatched twice within one epoch with identical resources, and again in the next epoch
- **THEN** each dispatch acquires its set from the arena
- **AND** all three receive the same set, with no new set created after the first

#### Scenario: Dictionary order does not change set identity

- **WHEN** the same kernel is dispatched twice in one epoch with the same resources supplied in two different dictionary orders
- **THEN** both dispatches acquire the same set
- **AND** the arena mints no second set

#### Scenario: A kernel holds no set between dispatches

- **WHEN** a kernel's dispatch returns
- **THEN** the kernel retains no descriptor-set handle
- **AND** nothing in the kernel needs to be released when the kernel is destroyed

### Requirement: Interface names are resolved from reflection, once per kernel

The mapping from an interface name to its descriptor set and binding SHALL be derived from the shader module's reflected decorations. The C++ side MUST NOT declare descriptor set or binding numbers.

Each declared interface name SHALL be resolved to a dense table when the kernel is created, and each dictionary entry SHALL be matched against that table once per dispatch. Dispatch-time work SHALL be proportional to the number of resources the caller supplies: it MUST NOT walk the shader's whole declared interface table, and MUST NOT build or hash a container keyed by every declared interface.

A kernel SHALL bind exactly one descriptor set, at set index 0 — where every compute shader in the engine declares its interfaces. Interfaces declared in other sets are outside this facility.

#### Scenario: A shader with arbitrary binding numbers needs no C++ change

- **WHEN** a shader declares its interfaces at non-contiguous binding numbers within set 0
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

Runtime shader reload is out of scope: a kernel MUST NOT be mutated or rebuilt, and the facility MUST NOT offer a reload entry point. SPIR-V whose contents change is reached through a new module identity rather than by rebuilding an existing kernel.

#### Scenario: Kernel outlives its requester

- **WHEN** a component that requested a kernel is destroyed while another component still dispatches it
- **THEN** the kernel remains valid and usable

#### Scenario: A repeated request does not rebuild the kernel

- **WHEN** a kernel is requested again for an identity the device already knows
- **THEN** the existing kernel is returned unchanged
- **AND** no pipeline, layout or shader module is created a second time

#### Scenario: A changed module is reached through a new identity

- **WHEN** a module's SPIR-V words change and are requested under an identity the device does not know
- **THEN** the request yields a kernel distinct from the one built from the previous words
- **AND** the kernel built from the previous words is unchanged

### Requirement: The kernel facility works without a frame loop

The kernel facility SHALL be usable with no presentation frame loop, no `FrameManager` and no render system. Creating a kernel and dispatching it SHALL require only the device facilities.

#### Scenario: Headless kernel dispatch

- **WHEN** a headless program creates a device context, requests a kernel and dispatches it to its own command buffer
- **THEN** the dispatch records and executes successfully
- **AND** no presentation or frame-loop type is required
