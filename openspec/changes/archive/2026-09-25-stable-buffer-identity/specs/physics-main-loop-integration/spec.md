## REMOVED Requirements

### Requirement: MainClass forwards model matrices buffer to SceneDataManager

**Reason**: The model matrices buffer is no longer produced by physics and handed to the render side. `SceneDataManager` owns it, and the assembly layer instead asks the physics side to write into it. A forwarding call, and the stable-ownership requirement that existed to make a borrowed pointer safe, therefore have no subject.

**Migration**: Delete the forwarding call from `MainClass::RunOneFrame`. The buffer the frame renders is obtained from `SceneDataManager`, and the frame's producer is invoked with it after the physics step (see *Physics pipeline in RunOneFrame*). Any caller that read the model matrices buffer from `PhysicsScene::GetGpuBuffers()` obtains it from `SceneDataManager` instead.

## MODIFIED Requirements

### Requirement: Automatic InitializePendingRigidBodies

After `ProcessEvents` and before `UpdateRendererData` in the main loop, the engine SHALL call `PhysicsSystem::InitializePendingRigidBodies` for each physics scene that has pending rigid body initializations. If no pending initializations exist, the call SHALL be a fast no-op.

#### Scenario: Pending rigid bodies are initialized before rendering

- **WHEN** `RigidBodyComponent::Init` enqueues rigid body initialization during `ProcessEvents`
- **THEN** `InitializePendingRigidBodies` is called before `UpdateRendererData` in the same frame
- **AND** the physics scene's rigid body GPU buffers are created and populated with initial transform data

#### Scenario: No pending initializations

- **WHEN** no rigid bodies have been enqueued for initialization
- **THEN** `InitializePendingRigidBodies` returns immediately without GPU work

### Requirement: Physics pipeline in RunOneFrame

`MainClass::RunOneFrame` SHALL execute the full physics pipeline in order: `PreGPUStep`, `GPUStep`, `PostGPUStep`. Physics compute, model matrix production and render graph passes SHALL share the same command buffer.

Model matrices SHALL be produced after the physics step and before the render graph's passes are recorded, in every rendered frame, independent of play, pause and simulation state. The producing call SHALL be skipped only when the main scene has no physics scene.

#### Scenario: Physics compute shares the frame command buffer

- **WHEN** `RunOneFrame` records a frame with a registered solver
- **THEN** physics compute passes and render graph passes are recorded on the same command buffer
- **AND** no physics compute is recorded on a separate command buffer

#### Scenario: Model matrices are produced before render passes

- **WHEN** `RunOneFrame` records a frame and the main scene has a physics scene
- **THEN** the physics side is asked to write model matrices into the render system's buffer after the physics step
- **AND** the render graph's passes are recorded afterwards, on the same command buffer

#### Scenario: A frame with no physics scene records no production

- **WHEN** `RunOneFrame` records a frame and the main scene has no physics scene (or physics disabled)
- **THEN** no model matrix production is recorded
- **AND** rendering proceeds against the render-owned buffer's initial contents

#### Scenario: Model matrices are produced while simulation is disabled

- **WHEN** the physics scene's simulation is disabled for the frame
- **THEN** model matrices are still produced from the current poses
- **AND** the render graph's passes observe up-to-date matrices

### Requirement: Physics GPUStep records into raw command buffer

`MainClass::RunOneFrame` SHALL call `physics->GPUStep(cb)` where `cb` is the raw `vk::CommandBuffer` obtained from the frame manager's main command buffer, and physics compute passes SHALL be recorded directly onto it.

#### Scenario: Physics step executes within shared command buffer

- **WHEN** `RunOneFrame` is called and a solver is registered
- **THEN** `PreGPUStep` is called before `cb.begin()`
- **AND** `GPUStep(cb)` is called between `cb.begin()` and `cb.end()`
- **AND** `PostGPUStep` is called after `cb.end()` and submit
- **AND** `render_graph->RecordAllPasses(cb)` is called after `GPUStep(cb)` on the same command buffer
