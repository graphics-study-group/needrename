# render-graph-model-matrix-input

## MODIFIED Requirements

### Requirement: ComplexRenderGraphBuilder accepts optional model matrices buffer

`ComplexRenderGraphBuilder::BuildDefaultRenderGraph` SHALL provide an overload accepting an optional model matrices buffer, defaulting to "none". When a buffer is supplied, the builder SHALL:
1. Import the buffer with `prev_access = MemoryAccessTypeBuffer(MemoryAccessTypeBufferBits::ShaderRandomWrite)`
2. Declare `UseBuffer(mm_handle, ShaderRandomRead)` on all shadow map passes
3. Declare `UseBuffer(mm_handle, ShaderRandomRead)` on the main lit pass

The supplied reference SHALL keep the buffer alive for as long as the built render graph holds it. When no buffer is supplied, the builder SHALL behave identically to the existing implementation.

#### Scenario: Physics model matrices used in shadow pass

- **WHEN** a non-null `model_matrices_buffer` is passed
- **THEN** each shadow map pass SHALL declare `UseBuffer(mm_handle, ShaderRandomRead)`
- **AND** a barrier from `COMPUTE_SHADER | SHADER_STORAGE_WRITE` to graphics read SHALL be inserted

#### Scenario: No model matrices buffer passed

- **WHEN** `BuildDefaultRenderGraph` is called with no `model_matrices_buffer` (default)
- **THEN** no model matrices buffer SHALL be imported
- **AND** the graph SHALL be identical to the pre-change implementation

#### Scenario: Reallocation after graph construction does not dangle

- **WHEN** the physics side replaces its model matrices buffer after the render graph has been built
- **THEN** the graph continues to reference a live buffer, because the buffer it imported is kept alive by the render side's reference
- **AND** no pass reads a destroyed buffer

### Requirement: SceneDataManager receives model matrices buffer from the assembly layer

The model matrices buffer produced by physics SHALL be forwarded to `SceneDataManager::SetModelMatricesBuffer()` by the `MainClass` assembly layer — no longer by the physics solver or `PhysicsScene::SyncGpuBuffers`. The forwarded value SHALL be a stably owned reference, not a borrowed raw pointer, so that a physics-side reallocation cannot leave the render side with a dangling reference.

#### Scenario: Model matrices buffer forwarded after physics step

- **WHEN** `MainClass::RunOneFrame` runs and the main scene has a physics scene with GPU buffers
- **THEN** after the physics flush/step, `SceneDataManager::SetModelMatricesBuffer()` is called with the physics scene's `model_matrices` buffer (from `GetGpuBuffers().model_matrices`)
- **AND** the buffer is forwarded even when the physics scene's buffer set was initialized this frame

#### Scenario: Physics no longer notifies SceneDataManager

- **WHEN** `XpbdGpuSolver::GPUStep` or `PhysicsScene::SyncGpuBuffers` runs
- **THEN** neither calls `SceneDataManager::SetModelMatricesBuffer`
- **AND** no Render header is included by the physics module

#### Scenario: Physics resizes model matrices while the render side holds it

- **WHEN** the rigid body slot count changes, causing `PhysicsScene` to replace its model matrices buffer
- **THEN** the previously forwarded buffer stays alive until neither the render side nor any in-flight submission references it
- **AND** the next forwarding call hands over the new buffer
