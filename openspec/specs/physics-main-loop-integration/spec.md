# physics-main-loop-integration Specification

## Purpose
Defines how `MainClass` integrates the physics GPU pipeline into the main loop: automatic solver registration on project load, per-frame `PreGPUStep` → `GPUStep` → `PostGPUStep` execution on the shared main command buffer, and the assembly-layer forward of the physics model matrices buffer into the render system.

## Requirements
### Requirement: Default solver auto-registration

Upon loading a project that contains physics components (`RigidBodyComponent` instances in the main scene), the engine SHALL automatically register a default `XpbdGpuSolver` with hardcoded configuration. If no physics components exist in the scene, no solver SHALL be registered.

#### Scenario: Project with physics objects loads successfully

- **WHEN** `MainClass::LoadProject` is called with a project containing at least one `RigidBodyComponent`
- **THEN** a `XpbdGpuSolver` is registered with `PhysicsSystem` for the main scene's physics scene
- **AND** the solver uses hardcoded `XpbdConfig` defaults

#### Scenario: Project without physics objects loads silently

- **WHEN** `MainClass::LoadProject` is called with a project containing zero `RigidBodyComponent` instances
- **THEN** no solver is registered
- **AND** no error or warning is emitted

### Requirement: Automatic InitializePendingRigidBodies

After `ProcessEvents` and before `UpdateRendererData` in the main loop, the engine SHALL call `PhysicsSystem::InitializePendingRigidBodies` for each physics scene that has pending rigid body initializations. If no pending initializations exist, the call SHALL be a fast no-op.

#### Scenario: Pending rigid bodies are initialized before rendering

- **WHEN** `RigidBodyComponent::Init` enqueues rigid body initialization during `ProcessEvents`
- **THEN** `InitializePendingRigidBodies` is called before `UpdateRendererData` in the same frame
- **AND** the SSBO model matrices GPU buffer is created and populated with initial transform data
- **AND** `SceneDataManager` is notified of the new model matrices buffer

#### Scenario: No pending initializations

- **WHEN** no rigid bodies have been enqueued for initialization
- **THEN** `InitializePendingRigidBodies` returns immediately without GPU work

### Requirement: Physics pipeline in RunOneFrame

`MainClass::RunOneFrame` SHALL execute the full physics pipeline in order: `PreGPUStep`, `GPUStep`, `PostGPUStep`. Physics compute and render graph passes SHALL share the same command buffer.

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

