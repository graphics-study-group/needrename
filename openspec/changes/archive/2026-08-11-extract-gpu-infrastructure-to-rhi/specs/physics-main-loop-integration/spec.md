# Physics Main Loop Integration

## Purpose

TBD - created by archiving change integrate-physics-into-main-loop. Update Purpose after archive.

## ADDED Requirements

### Requirement: MainClass forwards model matrices buffer to SceneDataManager

`MainClass::RunOneFrame` SHALL forward the physics model matrices buffer to the render system's `SceneDataManager` via `SetModelMatricesBuffer`, using the main scene's physics scene `GetGpuBuffers().model_matrices`. The forward SHALL happen after the physics flush/step and before render-graph recording in the same frame, and SHALL tolerate a null physics scene (skip the forward).

#### Scenario: Forward after physics step

- **WHEN** `RunOneFrame` executes with a registered physics scene containing GPU buffers
- **THEN** `SceneDataManager::SetModelMatricesBuffer` is called with the physics model matrices buffer pointer before render passes are recorded

#### Scenario: No physics scene skips forward

- **WHEN** `RunOneFrame` executes and the main scene has no physics scene (or physics disabled)
- **THEN** no call to `SetModelMatricesBuffer` is made and rendering proceeds with no model matrices buffer

### Requirement: Physics GPUStep records into raw command buffer

`MainClass::RunOneFrame` SHALL call `physics->GPUStep(cb)` where `cb` is the raw `vk::CommandBuffer` obtained from the frame manager's main command buffer, and physics compute passes SHALL be recorded directly onto it.

#### Scenario: Physics step executes within shared command buffer

- **WHEN** `RunOneFrame` is called and a solver is registered
- **THEN** `PreGPUStep` is called before `cb.begin()`
- **AND** `GPUStep(cb)` is called between `cb.begin()` and `cb.end()`
- **AND** `PostGPUStep` is called after `cb.end()` and submit
- **AND** `render_graph->RecordAllPasses(cb)` is called after `GPUStep(cb)` on the same command buffer
