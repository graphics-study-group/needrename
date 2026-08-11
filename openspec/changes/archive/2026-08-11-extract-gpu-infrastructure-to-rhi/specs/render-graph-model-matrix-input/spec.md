# Render Graph Model Matrix Input

## Purpose

Extends `ComplexRenderGraphBuilder` to optionally accept an external model matrices buffer (produced by GPU physics) and declare the correct buffer access in shadow map and lit passes so that physics-driven model matrices are used for rendering.

## MODIFIED Requirements

### Requirement: SceneDataManager receives model matrices buffer from the assembly layer

The model matrices buffer produced by physics SHALL be forwarded to `SceneDataManager::SetModelMatricesBuffer()` by the `MainClass` assembly layer — no longer by the physics solver or `PhysicsScene::SyncGpuBuffers`.

#### Scenario: Model matrices buffer forwarded after physics step

- **WHEN** `MainClass::RunOneFrame` runs and the main scene has a physics scene with GPU buffers
- **THEN** after the physics flush/step, `SceneDataManager::SetModelMatricesBuffer()` is called with the physics scene's `model_matrices` buffer pointer (from `GetGpuBuffers().model_matrices`)
- **AND** the buffer pointer is forwarded even when the physics scene's buffer set was initialized this frame

#### Scenario: Physics no longer notifies SceneDataManager

- **WHEN** `XpbdGpuSolver::GPUStep` or `PhysicsScene::SyncGpuBuffers` runs
- **THEN** neither calls `SceneDataManager::SetModelMatricesBuffer`
- **AND** no Render header is included by the physics module
