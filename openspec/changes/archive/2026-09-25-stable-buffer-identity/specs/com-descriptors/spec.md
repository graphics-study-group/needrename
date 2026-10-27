## MODIFIED Requirements

### Requirement: PhysicsScene SyncGpuBuffers

`PhysicsScene::SyncGpuBuffers(RenderSystem&)` SHALL create or resize all GPU buffers it owns to match the current SoA slot counts, upload all SoA and joint data via staging, and execute the submission immediately.

The model matrices buffer SHALL NOT be among them. It is owned by the render system, which sizes it on demand from the scene's rigid body slot count, and it is written by the solver's model matrix entry point rather than uploaded by this call.

#### Scenario: GPU buffers created on first sync

- **WHEN** `SyncGpuBuffers` is called for the first time after slot allocation
- **THEN** all physics-owned GPU buffers are created and populated with current SoA data

#### Scenario: Model matrices are not part of the scene's buffer set

- **WHEN** `SyncGpuBuffers` runs
- **THEN** it neither creates, resizes nor uploads a model matrices buffer
- **AND** the scene's exposed GPU buffer set contains no model matrices buffer
