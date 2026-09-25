## REMOVED Requirements

### Requirement: EditorRenderGraphBuilder accepts model matrices buffer

**Reason**: The editor builder no longer takes a model matrices parameter. The buffer is created and owned by the render system before the editor's graph is built, so the graph imports it unconditionally and never needs to be rebuilt when the producer starts writing. The shared reference that existed to keep a caller-supplied buffer alive across a physics-side reallocation has no subject.

**Migration**: Remove the `model_matrices_buffer` argument from `BuildEditorRenderGraph`. Delete the editor's one-shot graph rebuild and its `has_model_matrices_in_graph` flag: the first build is already complete, and the widget display textures taken from it stay valid.

## MODIFIED Requirements

### Requirement: Editor loop physics pipeline gated on play state

The editor main loop SHALL execute the physics step pipeline (`PreGPUStep`, `GPUStep`, `PostGPUStep`) only when `MainWindow::m_is_playing` is `true`. When not playing, the step pipeline SHALL be skipped entirely.

Model matrix production SHALL NOT be gated on the play state: the editor loop SHALL produce model matrices in every rendered frame, whether playing or stopped, because the physics-driven renderers' transforms are read from the same buffer in both states.

#### Scenario: Playing — physics pipeline runs

- **WHEN** the editor is in play mode (`m_is_playing == true`)
- **THEN** `PhysicsSystem::PreGPUStep` is called before `cb.begin()`
- **AND** `PhysicsSystem::GPUStep(cb)` is called between `cb.begin()` and `cb.end()`
- **AND** `PhysicsSystem::PostGPUStep` is called after `cb.end()` and submit

#### Scenario: Stopped — physics pipeline is skipped

- **WHEN** the editor is stopped (`m_is_playing == false`)
- **THEN** no `PreGPUStep`, `GPUStep`, or `PostGPUStep` calls are made
- **AND** model matrices are still produced for the frame before render passes are recorded
- **AND** the render graph executes normally without physics synchronization

### Requirement: Editor loop shares command buffer for physics and rendering

The editor loop SHALL share a single command buffer for physics compute, model matrix production and render graph passes, matching `MainClass::RunOneFrame` structure. Model matrix production SHALL be recorded after the physics step and before the render graph's passes, so that every pass reading the buffer observes matrices produced in the same frame.

#### Scenario: Physics and rendering on shared command buffer

- **WHEN** the editor is in play mode
- **THEN** `GPUStep(cb)` writes physics state
- **AND** model matrices are produced into the render-owned buffer on the same command buffer
- **AND** `RecordAllPasses(cb)` records all render passes on the same command buffer
- **AND** all of these happen between a single `cb.begin()` and `cb.end()` pair

#### Scenario: Stopped frame still produces matrices

- **WHEN** the editor is stopped and records a frame
- **THEN** model matrix production is recorded between `cb.begin()` and `RecordAllPasses(cb)`
- **AND** the passes reading the buffer observe matrices derived from the current scene poses

## ADDED Requirements

### Requirement: EditorRenderGraphBuilder imports the render-owned model matrices buffer

`EditorRenderGraphBuilder::BuildEditorRenderGraph` SHALL take no model matrices parameter. It SHALL import the render system's model matrices buffer unconditionally and declare read access on the shadowmap, scene lit, and game lit passes. Because the buffer object is owned by the render system and never replaced, the editor graph SHALL NOT need to be rebuilt when the producer starts writing.

#### Scenario: Render graph imports the buffer

- **WHEN** `BuildEditorRenderGraph` is called
- **THEN** the buffer is imported via `ImportExternalResource` with `ShaderRandomWrite` access
- **AND** shadowmap pass declares `UseBuffer` with `ShaderRandomRead`
- **AND** scene lit pass declares `UseBuffer` with `ShaderRandomRead`
- **AND** game lit pass declares `UseBuffer` with `ShaderRandomRead`

#### Scenario: The graph is built once

- **WHEN** the editor's first render graph has been built and the producer starts writing model matrices
- **THEN** no graph rebuild occurs
- **AND** no display texture is re-pointed as a consequence of model matrices becoming available

#### Scenario: Reallocation does not invalidate the editor graph

- **WHEN** the model matrices buffer's storage is reallocated after the editor's render graph has been built
- **THEN** the buffer the graph imported remains valid for the graph's lifetime
- **AND** no shadowmap, scene lit, or game lit pass reads a destroyed buffer
