## ADDED Requirements

### Requirement: RigidBodyComDescriptor struct
A `RigidBodyComDescriptor` struct SHALL exist in `PhysicsDescriptors.h` containing COM-space fields: `mass` (float), `static_friction` (float), `dynamic_friction` (float), `restitution` (float), `is_kinematic` (bool), `center_world_position` (vec4), `center_world_rotation` (vec4), `center_offset_local_position` (vec4), `inertia` (mat4), `inverse_inertia` (mat4), `linear_velocity` (vec4), `angular_velocity` (vec4), `external_force` (vec4), `external_torque` (vec4). All spatial fields SHALL be in COM world or COM-local space as indicated. The struct SHALL be submitted to `PhysicsScene::SubmitRigidBody`.

#### Scenario: COM descriptor built by Adaptor during Flush
- **WHEN** Adaptor::Flush processes a pending rigid body
- **THEN** a `RigidBodyComDescriptor` is constructed from the GO descriptor scalars and ComInertiaComputer output
- **AND** submitted to PhysicsScene

#### Scenario: COM descriptor carries computed values
- **WHEN** the descriptor is received by PhysicsScene
- **THEN** all spatial fields (center_world_position, center_offset_local, inertia, inverse_inertia) contain Adaptor-computed values ready for GPU upload

### Requirement: CollisionShapeComDescriptor struct
A `CollisionShapeComDescriptor` struct SHALL exist containing: `type` (uint32_t, CollisionShapeType enum value), `feature` (vec4), `local_position` (vec4, COM-local), `local_rotation` (vec4, COM-local), `bound_rigid_body` (uint32_t, INVALID_INDEX if unbound), `ignore_shape_indices` (vector<uint32_t>, resolved shape indices from Adaptor's filter resolution).

The struct SHALL NOT contain `world_position` or `world_rotation` fields. Solvers SHALL compute world pose from COM pose + shape local pose when needed.

#### Scenario: Shape COM descriptor submitted by Adaptor
- **WHEN** Adaptor::Flush processes a pending shape
- **THEN** a `CollisionShapeComDescriptor` is constructed from pending data and cached shape poses
- **AND** submitted to `PhysicsScene::SubmitCollisionShape`

#### Scenario: Bound shape has valid rigid body index
- **WHEN** a shape is bound to rigid body index 0
- **THEN** the descriptor's `bound_rigid_body` field is 0

#### Scenario: Unbound shape has INVALID_INDEX
- **WHEN** a shape is not bound to any rigid body
- **THEN** the descriptor's `bound_rigid_body` field is `INVALID_INDEX`

### Requirement: PhysicsScene Submit* interface
`PhysicsScene` SHALL provide unified `Submit*` methods that accept COM-space descriptors:
- `SubmitRigidBody(uint32_t idx, const RigidBodyComDescriptor&)` — writes all fields to SoA columns
- `SubmitCollisionShape(uint32_t idx, const CollisionShapeComDescriptor&)` — writes type, feature, local pose, bound RB, and filter indices
- `SubmitFixedJoint(uint32_t idx, const GpuFixedJoint&)` — writes COM-ready joint definition
- `SubmitHingeJoint(uint32_t idx, const GpuHingeJoint&)` — writes COM-ready joint definition

#### Scenario: Rigid body submitted
- **WHEN** `physics_scene.SubmitRigidBody(0, com_desc)` is called
- **THEN** SoA columns at index 0 are updated with descriptor values

#### Scenario: Shape submitted with filter indices
- **WHEN** `physics_scene.SubmitCollisionShape(1, com_desc)` is called with `ignore_shape_indices = {3, 5}`
- **THEN** the shape's filter data is updated to exclude shapes 3 and 5

### Requirement: PhysicsScene Allocate*Slot methods
`PhysicsScene` SHALL provide slot allocation methods that only create a slot (mark alive=1, zero-initialize columns) without requiring property values:
- `AllocateRigidBodySlot()` returns index
- `AllocateCollisionShapeSlot()` returns index
- `AllocateFixedJoint()` returns index (allocates with INVALID_INDEX placeholders)
- `AllocateHingeJoint()` returns index (allocates with INVALID_INDEX placeholders)

#### Scenario: Slot allocated with zero values
- **WHEN** `AllocateRigidBodySlot()` is called
- **THEN** a new index is returned with alive=1 and all SoA columns zero-initialized

### Requirement: PhysicsScene SyncGpuBuffers
`PhysicsScene::SyncGpuBuffers(RenderSystem&)` SHALL create or resize all GPU buffers matching the current SoA slot counts, upload all SoA and joint data via staging, and execute the submission immediately.

#### Scenario: GPU buffers created on first sync
- **WHEN** `SyncGpuBuffers` is called for the first time after slot allocation
- **THEN** all GPU buffers are created and populated with current SoA data

### Requirement: PhysicsScene retains shape-to-rigid-body mapping
`PhysicsScene` SHALL maintain `m_shape_to_rigid_body[]` as a simple `vector<uint32_t>` mapping shape index to rigid body index. This is the only topology mapping in PhysicsScene. The mapping SHALL be populated from `CollisionShapeComDescriptor::bound_rigid_body` during `SubmitCollisionShape`.

#### Scenario: Shape binding established via descriptor
- **WHEN** `SubmitCollisionShape(1, desc)` is called with `desc.bound_rigid_body = 0`
- **THEN** `m_shape_to_rigid_body[1]` is set to 0
