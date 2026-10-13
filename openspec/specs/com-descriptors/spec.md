# com-descriptors

## Purpose

Define the COM-space descriptor structs in `PhysicsDescriptors.h` (`RigidBodyComDescriptor`, `CollisionShapeComDescriptor`) and the `PhysicsScene` COM-space submission interface (`Submit*` methods, `Allocate*Slot` methods, `SyncGpuBuffers`, shape-to-rigid-body mapping) used after `PhysicsAdaptor::Flush`.

## Requirements

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
- `SubmitRigidBody(uint32_t idx, const RigidBodyComDescriptor&)` — writes the descriptor's fields to SoA columns, with the kinematic exception below
- `SubmitCollisionShape(uint32_t idx, const CollisionShapeComDescriptor&)` — writes type, feature, local pose, and bound RB
- `SubmitFixedJoint(uint32_t idx, const FixedJointComDescriptor&)` — writes COM-ready joint definition
- `SubmitHingeJoint(uint32_t idx, const HingeJointComDescriptor&)` — writes COM-ready joint definition

**The rigid-body SoA columns are the solver's view of a body, and a kinematic body is immovable.** When `com_desc.is_kinematic` is true, `SubmitRigidBody` SHALL write `0` into the `mass` column and a zero matrix into the `inverse_inertia` column regardless of the descriptor's values, and SHALL still write the descriptor's kinematic flag, pose, velocities and coefficients. The submitted descriptor and the component it came from SHALL remain unchanged, so the authored mass and inertia stay readable by the editor, the URDF importer and serialization.

Every constraint accumulate shader additionally treats a kinematic body by its flag, so correctness does not depend on this normalization having run; the normalization exists so that no consumer of these columns — present or future — has to repeat the rule.

`FixedJointComDescriptor` SHALL be the struct previously named `GpuFixedJoint` (48 bytes, std430), and `HingeJointComDescriptor` SHALL be the struct previously named `GpuHingeJoint` (80 bytes, std430); both SHALL be declared with `PHYSICS_API`.

#### Scenario: Rigid body submitted

- **WHEN** `physics_scene.SubmitRigidBody(0, com_desc)` is called with `is_kinematic = false`
- **THEN** SoA columns at index 0 are updated with descriptor values, mass and inverse inertia included

#### Scenario: Kinematic rigid body is mirrored as immovable

- **WHEN** `physics_scene.SubmitRigidBody(0, com_desc)` is called with `is_kinematic = true`, `mass = 5` and a non-zero `inverse_inertia`
- **THEN** the mass column at index 0 is `0` and the inverse-inertia column is the zero matrix
- **AND** the kinematic flag, pose, velocities and friction/restitution coefficients are written from the descriptor
- **AND** the descriptor passed by the caller is not modified

#### Scenario: Shape submitted with binding

- **WHEN** `physics_scene.SubmitCollisionShape(1, com_desc)` is called with `bound_rigid_body = 0`
- **THEN** `m_shape_to_rigid_body[1]` is set to 0

#### Scenario: Fixed joint submitted

- **WHEN** `physics_scene.SubmitFixedJoint(j, joint)` is called with a `FixedJointComDescriptor`
- **THEN** slot `j` in `m_fixed_joints` is populated with the provided values

#### Scenario: Hinge joint submitted

- **WHEN** `physics_scene.SubmitHingeJoint(j, joint)` is called with a `HingeJointComDescriptor`
- **THEN** slot `j` in `m_hinge_joints` is populated with the provided values

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

`PhysicsScene::SyncGpuBuffers(RenderSystem&)` SHALL create or resize all GPU buffers it owns to match the current SoA slot counts, upload all SoA and joint data via staging, and execute the submission immediately.

The model matrices buffer SHALL NOT be among them. It is owned by the render system, which sizes it on demand from the scene's rigid body slot count, and it is written by the solver's model matrix entry point rather than uploaded by this call.

#### Scenario: GPU buffers created on first sync
- **WHEN** `SyncGpuBuffers` is called for the first time after slot allocation
- **THEN** all physics-owned GPU buffers are created and populated with current SoA data

#### Scenario: Model matrices are not part of the scene's buffer set
- **WHEN** `SyncGpuBuffers` runs
- **THEN** it neither creates, resizes nor uploads a model matrices buffer
- **AND** the scene's exposed GPU buffer set contains no model matrices buffer

### Requirement: PhysicsScene retains shape-to-rigid-body mapping

`PhysicsScene` SHALL maintain `m_shape_to_rigid_body[]` as a simple `vector<uint32_t>` mapping shape index to rigid body index. This is the only topology mapping in PhysicsScene. The mapping SHALL be populated from `CollisionShapeComDescriptor::bound_rigid_body` during `SubmitCollisionShape`.

#### Scenario: Shape binding established via descriptor
- **WHEN** `SubmitCollisionShape(1, desc)` is called with `desc.bound_rigid_body = 0`
- **THEN** `m_shape_to_rigid_body[1]` is set to 0
