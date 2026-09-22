# editor-physics-pipeline

## MODIFIED Requirements

### Requirement: Editor loop physics pipeline gated on play state

The editor main loop SHALL execute the physics step through `PhysicsSystem::GPUStep(cb)` only when `MainWindow::m_is_playing` is `true`. When not playing, the physics step SHALL be skipped entirely. The editor SHALL NOT make any physics preparation or post-processing call outside command-buffer recording, in either state.

#### Scenario: Playing — physics pipeline runs

- **WHEN** the editor is in play mode (`m_is_playing == true`)
- **THEN** `PhysicsSystem::GPUStep(cb)` is called between `cb.begin()` and `cb.end()`
- **AND** no physics call is made before `cb.begin()` or after the frame is submitted

#### Scenario: Stopped — physics pipeline is skipped

- **WHEN** the editor is stopped (`m_is_playing == false`)
- **THEN** no `GPUStep` call is made
- **AND** the render graph executes normally without physics synchronization

### Requirement: Editor loop shares command buffer for physics and rendering

The editor loop SHALL share a single command buffer for physics compute (`GPUStep`) and render graph passes (`RecordAllPasses`), matching `MainClass::RunOneFrame` structure. Physics records its dispatches into the same command buffer the render passes use; it does not submit, and it does not run any phase outside that command buffer's recording scope.

#### Scenario: Physics and rendering on shared command buffer

- **WHEN** the editor is in play mode
- **THEN** `GPUStep(cb)` records physics dispatches into `cb`, including the model matrix pass that fills the physics-owned model matrices buffer
- **AND** `RecordAllPasses(cb)` records all render passes on the same command buffer
- **AND** both operations happen between a single `cb.begin()` and `cb.end()` pair
- **AND** the buffer is forwarded to the render system by the assembly layer, not by the physics step
