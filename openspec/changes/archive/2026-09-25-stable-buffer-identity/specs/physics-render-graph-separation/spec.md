## MODIFIED Requirements

### Requirement: External resource prev_access propagates state across RenderGraph boundaries

The rendering RenderGraph SHALL import shared buffers with `prev_access` reflecting the access its writer left the buffer in, whether that writer is another render graph or a compute pass recorded earlier in the same command buffer. A render graph SHALL NOT assume that a buffer it shares was written by another render graph.

For the model matrices buffer, whose writer is a physics compute pass recorded before the rendering passes and outside any render graph, the import SHALL use `ShaderRandomWrite`.

#### Scenario: Rendering RG imports buffer with physics output state

- **WHEN** a physics compute pass writes `model_matrices` with `ShaderRandomWrite`
- **AND** the rendering RenderGraph imports it with `prev_access = ShaderRandomWrite`
- **AND** the first rendering pass reads it with `ShaderRandomRead`
- **THEN** a barrier from `COMPUTE_SHADER | SHADER_STORAGE_WRITE` to the rendering pass SHALL be inserted

#### Scenario: A compute-pass writer needs no render graph of its own

- **WHEN** the writer of a shared buffer is a compute pass recorded directly into the frame's command buffer
- **THEN** the rendering RenderGraph still imports the buffer with the writer's access type
- **AND** no separate physics RenderGraph is required to carry the state
