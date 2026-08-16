# go-descriptors

## Purpose

Define the GO-space descriptor structs in `PhysicsDescriptors.h` used to carry data from Framework components (`RigidBodyComponent`, `CollisionShapeComponent`, `PhysicsConstraintComponent`) into the `PhysicsAdaptor`: `RigidBodyDescriptor`, `CollisionShapeDescriptor`, `FixedJointSubmitData`, and `HingeJointSubmitData`.

## Requirements

### Requirement: RigidBodyDescriptor struct

A `RigidBodyDescriptor` struct SHALL exist in `Framework/world/physics/PhysicsDescriptors.h` containing the following fields: `mass` (float), `static_friction` (float), `dynamic_friction` (float), `restitution` (float), `is_kinematic` (bool), `world_position` (vec3, GO world), `world_rotation` (quat, GO world), `linear_velocity` (vec3), `angular_velocity` (vec3), `external_force` (vec3), `external_torque` (vec3), `use_manual_inertia_com` (bool), `manual_inertia` (mat3), `manual_center_of_mass` (vec3, GO-local). The struct SHALL be copyable and movable.

The GO-space descriptors SHALL be Framework-internal transport structs carrying data from Framework components into `PhysicsAdaptor`; they SHALL NOT be part of the physics-interface (`PhysicsScene`) input contract.

#### Scenario: Descriptor built by RigidBodyComponent::Init
- **WHEN** `RigidBodyComponent::Init` runs
- **THEN** a `RigidBodyDescriptor` is constructed from component fields (m_mass, m_static_friction, etc.) and the GameObject's world transform

#### Scenario: Descriptor submitted to Adaptor
- **WHEN** `adaptor.SubmitRigidBody(idx, desc)` is called
- **THEN** the descriptor is stored in Adaptor's pending map for that index

### Requirement: CollisionShapeDescriptor struct

A `CollisionShapeDescriptor` struct SHALL exist containing: `type` (CollisionShapeType enum), `feature` (vec3, type-dependent: Box = half-extents, Sphere = radius.x, Cylinder = radius.x + half_height.y), `world_position` (vec3, GO world), `world_rotation` (quat, GO world), `ignore_collision_shapes` (vector<ComponentHandle>, handles of `CollisionShapeComponent`s that should not collide with this shape).

#### Scenario: Shape descriptor includes cylinder fallback
- **WHEN** a cylinder shape has non-uniform XY scale
- **THEN** the `BuildDescriptor` method in `CollisionShapeComponent` SHALL fall back to Box type with adjusted feature values
- **AND** this logic appears once in `BuildDescriptor`, not duplicated

#### Scenario: Descriptor carries ComponentHandle ignore list
- **WHEN** a shape has `m_ignore_collision_shapes = [comp_A, comp_B]`
- **THEN** the descriptor's `ignore_collision_shapes` field contains both handles

### Requirement: FixedJointSubmitData struct

`FixedJointSubmitData` SHALL contain: `obj1_index` (uint32_t), `obj2_index` (uint32_t), `compliance` (float), `initial_rel_pos_local` (vec3, GO-local), `initial_rel_rotation` (quat, GO-local).

#### Scenario: Fixed joint submit
- **WHEN** `PhysicsConstraintComponent::Init` processes a `FixedJointDef`
- **THEN** a `FixedJointSubmitData` is constructed with the resolved obj1/obj2 indices and computed GO-local relative transform
- **AND** submitted via `adaptor.SubmitFixedJoint(joint_idx, data)`

### Requirement: HingeJointSubmitData struct

`HingeJointSubmitData` SHALL contain: `obj1_index` (uint32_t), `obj2_index` (uint32_t), `compliance` (float), `hinge_axis_obj1` (vec3, GO-local), `hinge_anchor_obj1` (vec3, GO-local), `initial_rel_pos_local` (vec3, GO-local), `initial_rel_rotation` (quat, GO-local).

#### Scenario: Hinge joint submit
- **WHEN** `PhysicsConstraintComponent::Init` processes a `HingeJointDef`
- **THEN** a `HingeJointSubmitData` is constructed with hinge axis, anchor, and computed GO-local relative transform
- **AND** submitted via `adaptor.SubmitHingeJoint(joint_idx, data)`
