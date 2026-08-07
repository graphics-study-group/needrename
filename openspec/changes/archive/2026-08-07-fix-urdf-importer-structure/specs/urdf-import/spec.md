# urdf-import Delta Spec

## MODIFIED Requirements

### Requirement: Attach rigid body components with manual inertia

For each URDF link that has an `<inertial>` element, the system SHALL add a `RigidBodyComponent` to the link's GameObject with:

- `m_mass` set to the URDF mass value
- `m_use_manual_inertia_com` set to `true`
- `m_manual_inertia_diag` set to the diagonal components of the inertia tensor, rotated from the URDF inertial frame to the link frame when `<inertial>/<origin>` has non-zero `rpy`
- `m_manual_inertia_offdiag` set to the off-diagonal components of the inertia tensor, similarly rotated
- `m_manual_center_of_mass` set to the `<inertial>/<origin>` xyz position, converted to engine coordinates (GO-local space)

The inertia tensor SHALL be constructed as a 3x3 symmetric matrix `I` from the URDF `ixx, ixy, ixz, iyy, iyz, izz` values, then rotated using the formula:

```
R = mat3_cast(UrdfRpyToEngineQuat(inertial.origin_rpy))
I_link = transpose(R) * I * R
```

The diagonal and off-diagonal components extracted from `I_link` SHALL be stored in `m_manual_inertia_diag` and `m_manual_inertia_offdiag`.

Links without `<inertial>` SHALL NOT receive a `RigidBodyComponent`.

#### Scenario: A1 trunk gets rigid body with manual inertia and COM offset

- **WHEN** the A1 hierarchy is built
- **THEN** the `trunk` GameObject has a `RigidBodyComponent`
- **AND** `m_mass` is `6.0f`
- **AND** `m_use_manual_inertia_com` is `true`
- **AND** `m_manual_inertia_diag` is approximately (0.0158533, 0.0377999, 0.0456542)
- **AND** `m_manual_center_of_mass` equals `UrdfToEnginePos((0.0, 0.0041, -0.0005))` = `(-0.0041, 0.0, -0.0005)`

#### Scenario: A1 FR_thigh gets rigid body with manual inertia

- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_thigh` GameObject has a `RigidBodyComponent`
- **AND** `m_mass` is `1.013f`
- **AND** `m_manual_center_of_mass` equals `UrdfToEnginePos((-0.003237, 0.022327, -0.027326))` = `(-0.022327, -0.003237, -0.027326)`

#### Scenario: A1 base link has no rigid body

- **WHEN** the A1 hierarchy is built
- **THEN** the `base` GameObject has no `RigidBodyComponent` because it has no `<inertial>` element

#### Scenario: Inertia tensor is identity-rotated when rpy is zero

- **WHEN** a link has `<inertial>/<origin rpy="0 0 0">` and inertia tensor I
- **THEN** `m_manual_inertia_diag` and `m_manual_inertia_offdiag` are extracted from I without additional rotation (R = identity)

### Requirement: Attach collision shape components

For each URDF `<collision>` element, the system SHALL create a dedicated **child GameObject** of the link's GameObject. The child GameObject's name SHALL be `"{link_name}_collision_{i}"` where `i` is the 0-based index of the collision element within the link.

The child GameObject's local `Transform` SHALL be set from the collision element's `<origin>` (xyz and rpy), converted to engine coordinates. Scale SHALL be `(1, 1, 1)`.

A `CollisionShapeComponent` SHALL be added to the child GameObject with:

- `m_shape_type` mapped from URDF geometry type (box → Box, sphere → Sphere, cylinder → Cylinder)
- `m_feature` set to appropriate half-extents/radius (box: half of full size, sphere: radius, cylinder: (radius, half_length))
- `m_center` set to `(0, 0, 0)` — the collision offset is encoded in the child GO's Transform
- `m_rotation` set to identity quaternion — the collision rotation is encoded in the child GO's Transform

Collision elements with a mesh geometry (referencing DAE/STL files) SHALL be skipped in Phase 1 with a log info message.

#### Scenario: A1 trunk collision creates a child GO with box shape

- **WHEN** the A1 hierarchy is built
- **THEN** the `trunk` GameObject has a child GameObject named `trunk_collision_0`
- **AND** `trunk_collision_0` has a `CollisionShapeComponent` with type `Box`
- **AND** `m_feature` is approximately (0.1335, 0.097, 0.057) — half of size (0.267, 0.194, 0.114)
- **AND** `m_center` is `(0, 0, 0)`
- **AND** `m_rotation` is identity

#### Scenario: A1 foot collision creates a child GO with sphere shape

- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_foot` GameObject has a child GameObject named `FR_foot_collision_0`
- **AND** the child GO has a `CollisionShapeComponent` with type `Sphere`
- **AND** `m_feature.x` (radius) is `0.02f`

#### Scenario: A1 FR_thigh collision with offset creates a positioned child GO

- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_thigh` GameObject has a child GO `FR_thigh_collision_0`
- **AND** the child GO's local position is `UrdfToEnginePos((0, 0, -0.1))` = `(0, 0, -0.1)`
- **AND** the child GO's local rotation is `UrdfRpyToEngineQuat((0, 1.5708, 0))` — rotated 90° around Y

### Requirement: Attach joint constraint components

For each URDF joint with type `fixed` or `revolute` where **both** the parent link and child link have `<inertial>` (and therefore `RigidBodyComponent`), the system SHALL add a `PhysicsConstraintComponent` to the **child** link's GameObject.

For `fixed` joints, a `FixedJointDef` SHALL be created with:

- `m_obj2_handle` referencing the parent link's GameObject
- `m_compliance` set to `0.0f`

For `revolute` (or `continuous`) joints, a `HingeJointDef` SHALL be created with:

- `m_obj2_handle` referencing the parent link's GameObject
- `m_hinge_anchor_obj1` set to `(0, 0, 0)` — the pivot is at the child GO origin (which is the URDF joint origin)
- `m_hinge_axis_obj1` set to the URDF axis converted to engine coordinates via `UrdfAxisToEngine(joint.axis)` — no additional rotation needed (axis is already in child link frame)
- `m_compliance` set to `0.0f`

Joints where either the parent or child link has no `<inertial>` SHALL NOT receive a `PhysicsConstraintComponent`. The link without inertial shall have its collision shapes collected by the nearest rigid-body ancestor.

Joints of type `floating` or `prismatic` SHALL be skipped with a log warning.

#### Scenario: A1 revolute joint constraint is on child GO

- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_thigh` GameObject has a `PhysicsConstraintComponent` containing a `HingeJointDef`
- **AND** `m_obj2_handle` references the `FR_hip` GameObject (the parent)
- **AND** `m_hinge_anchor_obj1` is `(0, 0, 0)`
- **AND** `m_hinge_axis_obj1` is approximately `(-1, 0, 0)` in engine coordinates (converted from URDF axis `0 1 0`)

#### Scenario: A1 fixed joint constraint is on child GO

- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_foot` GameObject has a `PhysicsConstraintComponent` containing a `FixedJointDef`
- **AND** `m_obj2_handle` references the `FR_calf` GameObject (the parent)

#### Scenario: Joint with one side lacking inertial is skipped

- **WHEN** the A1 hierarchy is built with `FR_hip_fixed` connecting `FR_hip` (has inertial) to `FR_thigh_shoulder` (no inertial)
- **THEN** `FR_thigh_shoulder` has NO `PhysicsConstraintComponent`
- **AND** `FR_hip` has NO `PhysicsConstraintComponent` created for this joint
- **AND** `FR_thigh_shoulder` collision shapes are collected by `FR_hip`'s `RigidBodyComponent`

#### Scenario: floating_base between base (no inertial) and trunk (has inertial) is skipped

- **WHEN** the A1 hierarchy is built
- **THEN** no `PhysicsConstraintComponent` is created for the `floating_base` joint
- **AND** `trunk` has no constraint referencing `base` (base has no `RigidBodyComponent`)

### Requirement: Attach render mesh components from collision geometry using builtin assets

For each URDF `<collision>` element with a geometry type of box, sphere, or cylinder, the system SHALL create a dedicated **visual child GameObject** of the link's GameObject. The visual child GameObject's name SHALL be `"{link_name}_visual_{i}"` where `i` is the 0-based index of the collision element within the link.

The visual child GameObject's local `Transform` SHALL be set from the collision element's `<origin>` (xyz and rpy), converted to engine coordinates. Scale SHALL be set to the mesh scaling values appropriate for the geometry type:

- **Box**: scale = (size.x * 0.5, size.y * 0.5, size.z * 0.5) — builtin cube is 2×2×2 centered
- **Sphere**: scale = (radius, radius, radius) — builtin sphere has radius 1
- **Cylinder**: scale = (radius, radius, length * 0.5) — builtin cylinder has radius 1, height 2, Z-up

A `StaticMeshComponent` SHALL be added to the visual child GameObject with `m_mesh_asset` referencing the corresponding builtin mesh (`~/mesh/cube.asset`, `~/mesh/sphere.asset`, or `~/mesh/cylinder.asset`).

The `m_material_assets` vector SHALL contain one reference to a PBR solid color material from the builtin materials, selected deterministically based on a hash of the link name.

Collision elements with a mesh geometry (referencing DAE/STL files) SHALL be skipped in Phase 1 with a log info message.

#### Scenario: A1 trunk visual uses cube builtin mesh on a child GO

- **WHEN** the A1 hierarchy is built
- **THEN** the `trunk` GameObject has a visual child GO named `trunk_visual_0`
- **AND** `trunk_visual_0` has a `StaticMeshComponent`
- **AND** `m_mesh_asset` references a mesh with path `~/mesh/cube.asset`
- **AND** the child GO's local scale is (0.1335, 0.097, 0.057) — half the trunk box size

#### Scenario: A1 foot visual uses sphere builtin mesh on a child GO

- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_foot` GameObject has a visual child GO named `FR_foot_visual_0`
- **AND** the child GO has a `StaticMeshComponent`
- **AND** `m_mesh_asset` references a mesh with path `~/mesh/sphere.asset`
- **AND** the child GO's local scale is (0.02, 0.02, 0.02) — the foot sphere radius in all axes

#### Scenario: Same link always gets same material color

- **WHEN** the A1 hierarchy is built twice
- **THEN** the `trunk_visual_0` GameObject receives the same builtin material in both builds

## REMOVED Requirements

### Requirement: A1 floating_base joint creates a fixed constraint

**Reason**: The `floating_base` joint in A1 connects `base` (no `<inertial>`, no `RigidBodyComponent`) to `trunk` (has inertial). Per the modified joint constraint rule, constraints are only created when both sides have `RigidBodyComponent`. The joint is correctly skipped.

**Migration**: No migration needed. `trunk` becomes a freely-moving rigid body (correct for a floating-base robot). The `base` link has no collision geometry; its tiny visual box is harmless.
