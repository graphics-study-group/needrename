# physics-main-loop-integration

## MODIFIED Requirements

### Requirement: Physics pipeline in RunOneFrame

`MainClass::RunOneFrame` SHALL execute the physics step through a single call to `PhysicsSystem::GPUStep(cb)` recorded into the frame's main command buffer. It SHALL NOT call any physics preparation or post-processing phase outside command-buffer recording. Physics compute, model matrix production and render graph passes SHALL share the same command buffer.

Model matrices SHALL be produced after the physics step and before the render graph's passes are recorded, in every rendered frame, independent of play, pause and simulation state, by the caller invoking `PhysicsSystem::GPUCalcModelMatrices` with the render system's buffer. The producing call SHALL be skipped only when the main scene has no physics scene.

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

#### Scenario: Model matrices are produced before render passes

- **WHEN** `RunOneFrame` records a frame and the main scene has a physics scene
- **THEN** the physics side is asked to write model matrices into the render system's buffer after the physics step
- **AND** the render graph's passes are recorded afterwards, on the same command buffer

#### Scenario: A frame with no physics scene records no production

- **WHEN** `RunOneFrame` records a frame and the main scene has no physics scene (or physics disabled)
- **THEN** no model matrix production is recorded
- **AND** rendering proceeds against the render-owned buffer's initial contents

#### Scenario: Model matrices are produced while simulation is disabled

- **WHEN** the physics scene's simulation is disabled for the frame
- **THEN** model matrices are still produced from the current poses
- **AND** the render graph's passes observe up-to-date matrices

### Requirement: Physics GPUStep records into raw command buffer

`MainClass::RunOneFrame` SHALL call `physics->GPUStep(cb)` where `cb` is the raw `vk::CommandBuffer` obtained from the frame manager's main command buffer, and physics compute passes SHALL be recorded directly onto it.

#### Scenario: Physics step executes within shared command buffer

- **WHEN** `RunOneFrame` is called and a solver is registered
- **THEN** `GPUStep(cb)` is called between `cb.begin()` and `cb.end()`
- **AND** `render_graph->RecordAllPasses(cb)` is called after `GPUStep(cb)` on the same command buffer
- **AND** no physics-related call is made outside that `begin()` / `end()` pair
