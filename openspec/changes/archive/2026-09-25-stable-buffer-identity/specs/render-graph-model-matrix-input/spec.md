## REMOVED Requirements

### Requirement: ComplexRenderGraphBuilder accepts optional model matrices buffer

**Reason**: The builder no longer takes a model matrices parameter. The buffer is created and owned by the render system before any render graph is built, so it is always available and the graph imports it unconditionally. The build-time branch, and the shared reference that existed to keep a caller-supplied buffer alive across a physics-side reallocation, have no subject.

**Migration**: Remove the `model_matrices_buffer` argument from every `BuildDefaultRenderGraph` call. Callers that passed a physics-owned buffer pass nothing; the graph obtains the render system's model matrices buffer itself.

### Requirement: SceneDataManager receives model matrices buffer from the assembly layer

**Reason**: `SceneDataManager` owns the model matrices buffer instead of receiving one. Nothing forwards a buffer to it, and no setter exists. The stable-ownership clause existed to make a forwarded reference survive a physics-side resize, which can no longer happen because physics does not own the buffer.

**Migration**: Delete every `SetModelMatricesBuffer` call and the parameter it passed. Code that needs the buffer obtains it from `SceneDataManager`; code that produces matrices is invoked with it.

## ADDED Requirements

### Requirement: ComplexRenderGraphBuilder imports the render-owned model matrices buffer

`ComplexRenderGraphBuilder::BuildDefaultRenderGraph` SHALL take no model matrices parameter. It SHALL import the render system's model matrices buffer unconditionally and SHALL:

1. Import the buffer with `prev_access = MemoryAccessTypeBuffer(MemoryAccessTypeBufferBits::ShaderRandomWrite)`
2. Declare `UseBuffer(mm_handle, ShaderRandomRead)` on all shadow map passes
3. Declare `UseBuffer(mm_handle, ShaderRandomRead)` on the main lit pass

The import SHALL NOT depend on any build-time condition, and the built graph SHALL NOT need to be rebuilt when the producer starts writing. The graph SHALL hold a reference to the buffer object, which stays valid for the graph's lifetime because the object is never replaced — only its storage may be reallocated in place.

#### Scenario: Physics model matrices used in shadow pass

- **WHEN** the default render graph is built
- **THEN** each shadow map pass SHALL declare `UseBuffer(mm_handle, ShaderRandomRead)`
- **AND** a barrier from `COMPUTE_SHADER | SHADER_STORAGE_WRITE` to graphics read SHALL be inserted

#### Scenario: The import is unconditional

- **WHEN** `BuildDefaultRenderGraph` is called before any physics work has been recorded
- **THEN** the model matrices buffer SHALL still be imported and declared on the shadow map and lit passes
- **AND** the built graph SHALL NOT be rebuilt when the producer later starts writing the buffer

#### Scenario: Reallocation after graph construction does not dangle

- **WHEN** the model matrices buffer's storage is reallocated after the render graph has been built
- **THEN** the graph SHALL continue to reference a live buffer, because the buffer object is not replaced
- **AND** the buffer handle the graph resolves when recording SHALL be the current one

### Requirement: SceneDataManager owns the model matrices buffer

`SceneDataManager` SHALL uniquely own the model matrices buffer: it SHALL create it once, SHALL never replace the buffer object, and SHALL expose it so that the frame's producer can write into it. This SHALL be the only model matrices storage in the engine.

`SceneDataManager` SHALL size the buffer on demand through an element-count entry point, growing its storage in place when a larger capacity is requested, and SHALL start from an initial reservation so that early frames do not reallocate repeatedly. The buffer SHALL be initialized once at creation to identity matrices, so that a frame which produces nothing renders a wrong transform rather than reading undefined memory.

`SceneDataManager` SHALL NOT accept a forwarded model matrices buffer, and no setter for one SHALL exist. Physics SHALL NOT hold a reference to this buffer between calls, and descriptor binding 2 SHALL always point at it.

#### Scenario: The buffer exists before any producer runs

- **WHEN** the scene descriptor set is written for a frame in which no physics work has been produced
- **THEN** binding 2 points at the render-owned buffer
- **AND** the buffer holds the identity matrices written at creation

#### Scenario: Capacity grows on demand without moving the object

- **WHEN** the producer reports a rigid body slot count larger than the buffer's current capacity
- **THEN** the buffer's storage is reallocated in place to hold at least that many matrices
- **AND** the buffer object stays at the same address

#### Scenario: No forwarded buffer is accepted

- **WHEN** the engine is built and the assembly layers are inspected
- **THEN** `SceneDataManager` exposes no setter that accepts a model matrices buffer
- **AND** no code outside the render system assigns the model matrices buffer
