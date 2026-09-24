# editor-physics-pipeline Specification

## Purpose

Defines how the editor main loop drives the physics pipeline: the pipeline runs only while the editor is playing, shares the frame's command buffer with rendering, forwards the physics model matrices buffer into the editor render graph, and is toggled by the editor's start and stop callbacks.

## Requirements
### Requirement: Editor loop physics pipeline gated on play state

The editor main loop SHALL execute the full physics pipeline (`PreGPUStep`, `GPUStep`, `PostGPUStep`) only when `MainWindow::m_is_playing` is `true`. When not playing, the physics pipeline SHALL be skipped entirely.

#### Scenario: Playing — physics pipeline runs

- **WHEN** the editor is in play mode (`m_is_playing == true`)
- **THEN** `PhysicsSystem::PreGPUStep` is called before `cb.begin()`
- **AND** `PhysicsSystem::GPUStep(cb)` is called between `cb.begin()` and `cb.end()`
- **AND** `PhysicsSystem::PostGPUStep` is called after `cb.end()` and submit

#### Scenario: Stopped — physics pipeline is skipped

- **WHEN** the editor is stopped (`m_is_playing == false`)
- **THEN** no `PreGPUStep`, `GPUStep`, or `PostGPUStep` calls are made
- **AND** the render graph executes normally without physics synchronization

### Requirement: Editor loop shares command buffer for physics and rendering

The editor loop SHALL share a single command buffer for physics compute (`GPUStep`) and render graph passes (`RecordAllPasses`), matching `MainClass::RunOneFrame` structure.

#### Scenario: Physics and rendering on shared command buffer

- **WHEN** the editor is in play mode
- **THEN** `GPUStep(cb)` writes physics model matrices to the SSBO
- **AND** `RecordAllPasses(cb)` records all render passes on the same command buffer
- **AND** both operations happen between a single `cb.begin()` and `cb.end()` pair

### Requirement: EditorRenderGraphBuilder accepts model matrices buffer

`EditorRenderGraphBuilder::BuildEditorRenderGraph` SHALL accept an optional model matrices buffer parameter, defaulting to "none". When a buffer is supplied, the buffer SHALL be imported as an external render graph resource and declared with read access on shadowmap, scene lit, and game lit passes. The supplied reference SHALL keep the buffer alive for as long as the built render graph holds it, so that a physics-side reallocation cannot leave the editor's graph pointing at a destroyed buffer.

#### Scenario: SSBO available — render graph imports it

- **WHEN** `BuildEditorRenderGraph` is called with a non-null `model_matrices_buffer`
- **THEN** the buffer is imported via `ImportExternalResource` with `ShaderRandomWrite` access
- **AND** shadowmap pass declares `UseBuffer` with `ShaderRandomRead`
- **AND** scene lit pass declares `UseBuffer` with `ShaderRandomRead`
- **AND** game lit pass declares `UseBuffer` with `ShaderRandomRead`

#### Scenario: SSBO not available — render graph skips import

- **WHEN** `BuildEditorRenderGraph` is called with a `nullptr` `model_matrices_buffer`
- **THEN** no `ImportExternalResource` or `UseBuffer` calls are made for model matrices
- **AND** no export barriers are generated for the model matrices buffer

#### Scenario: Physics resizes the buffer while the editor graph holds it

- **WHEN** the physics scene replaces its model matrices buffer after the editor's render graph has been built
- **THEN** the buffer the graph imported remains alive for the graph's lifetime
- **AND** no shadowmap, scene lit, or game lit pass reads a destroyed buffer

### Requirement: Start callback enables simulation

The editor `m_OnStart` callback SHALL clear the event queue, queue `AddInitEvent` for all components, and call `PhysicsScene::SetSimulationEnabled(true)`.

#### Scenario: Play button pressed

- **WHEN** the user clicks the Start button
- **THEN** `Scene::ClearEventQueue` is called
- **AND** `Scene::AddInitEvent` is called for all existing components
- **AND** `PhysicsScene::SetSimulationEnabled(true)` is called on the main scene's physics scene

### Requirement: Stop callback disables simulation

The editor `m_OnStop` callback SHALL call `PhysicsScene::SetSimulationEnabled(false)`.

#### Scenario: Stop button pressed

- **WHEN** the user clicks the Stop button
- **THEN** `PhysicsScene::SetSimulationEnabled(false)` is called on the main scene's physics scene
- **AND** all `model_matrix_active` flags are cleared as a side effect

