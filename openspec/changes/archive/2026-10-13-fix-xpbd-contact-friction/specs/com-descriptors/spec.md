## MODIFIED Requirements

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
