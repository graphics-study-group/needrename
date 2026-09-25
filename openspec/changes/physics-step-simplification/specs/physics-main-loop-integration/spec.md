# physics-main-loop-integration

## MODIFIED Requirements

### Requirement: Physics pipeline in RunOneFrame

`MainClass::RunOneFrame` SHALL execute the physics step through a single call to `PhysicsSystem::GPUStep(cb)` recorded into the frame's main command buffer. It SHALL NOT call any physics preparation or post-processing phase outside command-buffer recording. Physics compute and render graph passes SHALL share the same command buffer.

#### Scenario: Physics step is a single recorded call

- **WHEN** `RunOneFrame` executes with a registered solver
- **THEN** `physics->GPUStep(cb)` is called exactly once for the frame
- **AND** no physics preparation call is made before the command buffer is recording
- **AND** no physics post-processing call is made after it is submitted

#### Scenario: Geometry changes are absorbed without a separate phase

- **WHEN** the number of rigid bodies, shapes or joints changes between two frames
- **THEN** the physics step still consists of the one `GPUStep(cb)` call
- **AND** the buffers the new geometry needs are sized within that call

#### Scenario: Physics compute shares the frame command buffer

- **WHEN** `RunOneFrame` records a frame with a registered solver
- **THEN** physics compute passes and render graph passes are recorded on the same command buffer
- **AND** no physics compute is recorded on a separate command buffer

### Requirement: Physics GPUStep records into raw command buffer

`MainClass::RunOneFrame` SHALL call `physics->GPUStep(cb)` where `cb` is the raw `vk::CommandBuffer` obtained from the frame manager's main command buffer, and physics compute passes SHALL be recorded directly onto it.

#### Scenario: Physics step executes within shared command buffer

- **WHEN** `RunOneFrame` is called and a solver is registered
- **THEN** `GPUStep(cb)` is called between `cb.begin()` and `cb.end()`
- **AND** `render_graph->RecordAllPasses(cb)` is called after `GPUStep(cb)` on the same command buffer
- **AND** no physics-related call is made outside that `begin()` / `end()` pair
