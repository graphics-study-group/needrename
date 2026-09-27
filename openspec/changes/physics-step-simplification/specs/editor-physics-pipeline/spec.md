# editor-physics-pipeline

## MODIFIED Requirements

### Requirement: Editor loop physics pipeline gated on play state

The editor main loop SHALL execute the physics step through `PhysicsSystem::GPUStep(cb)` only when `MainWindow::m_is_playing` is `true`. When not playing, the physics step SHALL be skipped entirely. The editor SHALL NOT make any physics preparation or post-processing call outside command-buffer recording, in either state.

Model matrix production SHALL NOT be gated on the play state: the editor loop SHALL produce model matrices in every rendered frame, whether playing or stopped, because the physics-driven renderers' transforms are read from the same buffer in both states.

#### Scenario: Playing — physics pipeline runs

- **WHEN** the editor is in play mode (`m_is_playing == true`)
- **THEN** `PhysicsSystem::GPUStep(cb)` is called between `cb.begin()` and `cb.end()`
- **AND** no physics call is made before `cb.begin()` or after the frame is submitted

#### Scenario: Stopped — physics pipeline is skipped

- **WHEN** the editor is stopped (`m_is_playing == false`)
- **THEN** no `GPUStep` call is made
- **AND** model matrices are still produced for the frame before render passes are recorded
- **AND** the render graph executes normally without physics synchronization

### Requirement: Editor loop shares command buffer for physics and rendering

The editor loop SHALL share a single command buffer for physics compute, model matrix production and render graph passes (`RecordAllPasses`), matching `MainClass::RunOneFrame` structure. Model matrix production SHALL be recorded after the physics step and before the render graph's passes, so that every pass reading the buffer observes matrices produced in the same frame. Physics records its dispatches into the same command buffer the render passes use; it does not submit, and it does not run any phase outside that command buffer's recording scope.

#### Scenario: Physics and rendering on shared command buffer

- **WHEN** the editor is in play mode
- **THEN** `GPUStep(cb)` records physics dispatches into `cb`
- **AND** model matrices are produced into the render-owned buffer on the same command buffer
- **AND** `RecordAllPasses(cb)` records all render passes on the same command buffer
- **AND** all of these happen between a single `cb.begin()` and `cb.end()` pair

#### Scenario: Stopped frame still produces matrices

- **WHEN** the editor is stopped and records a frame
- **THEN** model matrix production is recorded between `cb.begin()` and `RecordAllPasses(cb)`
- **AND** the passes reading the buffer observe matrices derived from the current scene poses
