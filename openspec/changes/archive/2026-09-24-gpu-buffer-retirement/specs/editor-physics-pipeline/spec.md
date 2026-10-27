# editor-physics-pipeline

## MODIFIED Requirements

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
