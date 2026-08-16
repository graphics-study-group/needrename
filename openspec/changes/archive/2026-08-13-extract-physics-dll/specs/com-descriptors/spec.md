# com-descriptors

## MODIFIED Requirements

### Requirement: RigidBodyComDescriptor struct

A `RigidBodyComDescriptor` struct SHALL exist in `engine/Physics/PhysicsDescriptors.h` containing COM-space fields: `mass` (float), `static_friction` (float), `dynamic_friction` (float), `restitution` (float), `is_kinematic` (bool), `center_world_position` (vec4), `center_world_rotation` (vec4), `center_offset_local_position` (vec4), `inertia` (mat4), `inverse_inertia` (mat4), `linear_velocity` (vec4), `angular_velocity` (vec4), `external_force` (vec4), `external_torque` (vec4). All spatial fields SHALL be in COM world or COM-local space as indicated. The struct SHALL be submitted to `PhysicsScene::SubmitRigidBody`. The struct SHALL be declared with `PHYSICS_API`.

#### Scenario: COM descriptor built by Adaptor during Flush
- **WHEN** Adaptor::Flush processes a pending rigid body
- **THEN** a `RigidBodyComDescriptor` is constructed from the GO descriptor scalars and ComInertiaComputer output
- **AND** submitted to PhysicsScene

#### Scenario: COM descriptor carries computed values
- **WHEN** the descriptor is received by PhysicsScene
- **THEN** all spatial fields (center_world_position, center_offset_local, inertia, inverse_inertia) contain Adaptor-computed values ready for GPU upload

### Requirement: CollisionShapeComDescriptor struct

A `CollisionShapeComDescriptor` struct SHALL exist in `engine/Physics/PhysicsDescriptors.h` containing: `type` (uint32_t, CollisionShapeType enum value), `feature` (vec4), `local_position` (vec4, COM-local), `local_rotation` (vec4, COM-local), `bound_rigid_body` (uint32_t, INVALID_INDEX if unbound).

The struct SHALL NOT contain `world_position`, `world_rotation`, or filter-list fields. Solvers SHALL compute world pose from COM pose + shape local pose when needed. Collision filter data SHALL be submitted separately via `PhysicsScene::SetShapeFilters`. The struct SHALL be declared with `PHYSICS_API`.

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
- `SubmitCollisionShape(uint32_t idx, const CollisionShapeComDescriptor&)` — writes type, feature, local pose, and bound RB
- `SubmitFixedJoint(uint32_t idx, const FixedJointComDescriptor&)` — writes COM-ready joint definition
- `SubmitHingeJoint(uint32_t idx, const HingeJointComDescriptor&)` — writes COM-ready joint definition

`FixedJointComDescriptor` SHALL be the struct previously named `GpuFixedJoint` (48 bytes, std430), and `HingeJointComDescriptor` SHALL be the struct previously named `GpuHingeJoint` (80 bytes, std430); both SHALL be declared with `PHYSICS_API`.

#### Scenario: Rigid body submitted
- **WHEN** `physics_scene.SubmitRigidBody(0, com_desc)` is called
- **THEN** SoA columns at index 0 are updated with descriptor values

#### Scenario: Shape submitted with binding
- **WHEN** `physics_scene.SubmitCollisionShape(1, com_desc)` is called with `bound_rigid_body = 0`
- **THEN** `m_shape_to_rigid_body[1]` is set to 0

#### Scenario: Fixed joint submitted
- **WHEN** `physics_scene.SubmitFixedJoint(j, joint)` is called with a `FixedJointComDescriptor`
- **THEN** slot `j` in `m_fixed_joints` is populated with the provided values

#### Scenario: Hinge joint submitted
- **WHEN** `physics_scene.SubmitHingeJoint(j, joint)` is called with a `HingeJointComDescriptor`
- **THEN** slot `j` in `m_hinge_joints` is populated with the provided values
