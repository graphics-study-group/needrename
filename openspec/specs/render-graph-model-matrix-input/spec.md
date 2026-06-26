# Render Graph Model Matrix Input

## Purpose

Extends `ComplexRenderGraphBuilder` to optionally accept an external model matrices buffer (produced by GPU physics) and declare the correct buffer access in shadow map and lit passes so that physics-driven model matrices are used for rendering.

## Requirements

### Requirement: ComplexRenderGraphBuilder accepts optional model matrices buffer

`ComplexRenderGraphBuilder::BuildDefaultRenderGraph` SHALL provide an overload accepting `const ComputeBuffer *model_matrices_buffer = nullptr`. When non-null, the builder SHALL:
1. Import the buffer with `prev_access = MemoryAccessTypeBuffer(MemoryAccessTypeBufferBits::ShaderRandomWrite)`
2. Declare `UseBuffer(mm_handle, ShaderRandomRead)` on all shadow map passes
3. Declare `UseBuffer(mm_handle, ShaderRandomRead)` on the main lit pass

When `model_matrices_buffer` is `nullptr`, the builder SHALL behave identically to the existing implementation.

#### Scenario: Physics model matrices used in shadow pass

- **WHEN** a non-null `model_matrices_buffer` is passed
- **THEN** each shadow map pass SHALL declare `UseBuffer(mm_handle, ShaderRandomRead)`
- **AND** a barrier from `COMPUTE_SHADER | SHADER_STORAGE_WRITE` to graphics read SHALL be inserted

#### Scenario: No model matrices buffer passed

- **WHEN** `BuildDefaultRenderGraph` is called with `model_matrices_buffer = nullptr` (default)
- **THEN** no model matrices buffer SHALL be imported
- **AND** the graph SHALL be identical to the pre-change implementation

### Requirement: SceneDataManager receives model matrices buffer from physics

When a physics solver produces model matrices, `SceneDataManager::SetModelMatricesBuffer()` SHALL be called with the physics scene's `model_matrices` buffer pointer after the scene's GPU buffers are initialized.

#### Scenario: Model matrices buffer set before rendering

- **WHEN** `DummySolver::BuildRenderGraph()` is called and the scene has rigid bodies
- **THEN** `SceneDataManager::SetModelMatricesBuffer()` SHALL be called with the scene's model matrices buffer pointer
