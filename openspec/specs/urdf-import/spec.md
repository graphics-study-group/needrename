# urdf-import

## Purpose

Import URDF robot descriptions into the engine: parse URDF XML into an intermediate representation, build a GameObject hierarchy mirroring the link/joint tree, attach physics components (rigid bodies, collision shapes, joint constraints), attach render meshes from collision geometry, apply parent-child collision filtering, and persist the result as a SceneAsset prefab.

## Requirements

### Requirement: Parse URDF XML into intermediate representation

The system SHALL parse a URDF XML file and produce a structured intermediate representation containing all links, joints, and materials defined in the `<robot>` element.

The parser SHALL extract from each `<link>`:
- `name` attribute
- `<inertial>`: mass, inertia tensor (ixx, ixy, ixz, iyy, iyz, izz), and origin (xyz + rpy) relative to link frame
- `<visual>` list: origin (xyz + rpy) and geometry (box, sphere, cylinder, or mesh)
- `<collision>` list: origin (xyz + rpy) and geometry (box, sphere, cylinder, or mesh)

The parser SHALL extract from each `<joint>`:
- `name`, `type` (fixed, revolute, continuous, floating, prismatic), `parent` link, `child` link
- `<origin>`: xyz and rpy defining child link frame relative to parent link frame
- `<axis>`: xyz vector for revolute/continuous/prismatic joints
- `<dynamics>`: damping and friction (optional)
- `<limit>`: lower, upper, effort, velocity (optional)

`<material>` elements with `name` and `color rgba` SHALL be collected into a material map.

The parser SHALL resolve `package://` URI scheme in mesh filenames by mapping the package name to a configurable asset root directory.

#### Scenario: Parse A1 URDF produces correct link count
- **WHEN** `a1.urdf` is parsed
- **THEN** the intermediate representation contains 22 links and 22 joints

#### Scenario: Parse URDF captures inertia tensor
- **WHEN** `a1.urdf` is parsed and the trunk link is inspected
- **THEN** its inertial mass is `6.0`
- **AND** its inertia diagonal is approximately (0.0158533, 0.0377999, 0.0456542)

#### Scenario: Parse URDF captures joint limits
- **WHEN** `a1.urdf` is parsed and `FR_thigh_joint` is inspected
- **THEN** its type is `revolute`
- **AND** its lower limit is `-1.0471975512`
- **AND** its upper limit is `4.18879020479`

#### Scenario: Parse URDF captures collision geometry
- **WHEN** `a1.urdf` is parsed and the trunk link is inspected
- **THEN** its collision geometry is a box with size `(0.267, 0.194, 0.114)`

### Requirement: Build robot into a caller-provided scene with configurable options

The system SHALL provide a public `UrdfLoader::BuildRobotScene` function that builds the complete robot hierarchy (GameObjects, rigid bodies, collision shapes, joint constraints, and optional visual meshes) directly into a caller-provided `Scene`.

The function SHALL accept:
- `robot`: the parsed `UrdfRobot` intermediate representation
- `scene`: the target `Scene` the hierarchy is built into
- `root_parent`: an optional parent `GameObject` for the root link (null = no parent)
- `options` (`UrdfBuildOptions`): overall placement (`position`, `rotation`), uniform physics coefficients (`static_friction`, `dynamic_friction`, `restitution`), and a `with_visuals` switch

The function SHALL NOT create a temporary scene, SHALL NOT call `FlushCmdQueue`, and SHALL NOT save any asset. Queue flushing and scene freezing remain the caller's responsibility.

The function SHALL return an `UrdfBuiltRobot` containing:
- `link_objects`: link name → `ObjectHandle`, containing ONLY links that received a `RigidBodyComponent` (links with `<inertial>`)
- `joint_objects`: joint name → `{parent, child}` `ObjectHandle` pair, containing ONLY joints that produced a `PhysicsConstraintComponent` (fixed/revolute/continuous with rigid bodies on both ends)

#### Scenario: Build into provided scene does not flush or save
- **WHEN** `BuildRobotScene` is called with a caller-provided scene
- **THEN** no asset file is written
- **AND** no `FlushCmdQueue` happens inside the function

#### Scenario: Link map contains only rigid-body links
- **WHEN** the A1 robot is built into a scene
- **THEN** `link_objects` contains `trunk`, `imu_link`, `FR_hip`, `FR_thigh`, `FR_calf`, and `FR_foot` (all 18 links with `<inertial>`)
- **AND** `link_objects` does NOT contain `base` (no `<inertial>`)
- **AND** `link_objects` does NOT contain `FR_thigh_shoulder` (no `<inertial>`)

#### Scenario: Joint map contains only realized constraints
- **WHEN** the A1 robot is built into a scene
- **THEN** `joint_objects` contains `FR_thigh_joint` with parent = `FR_hip` handle and child = `FR_thigh` handle
- **AND** `joint_objects` does NOT contain `floating_base` (parent `base` has no rigid body)
- **AND** `joint_objects` does NOT contain `FR_hip_fixed` (child `FR_thigh_shoulder` has no rigid body)

### Requirement: Build options control placement and physical coefficients

For each `RigidBodyComponent` created during the build, the system SHALL set:
- `m_static_friction` = `options.static_friction`
- `m_dynamic_friction` = `options.dynamic_friction`
- `m_restitution` = `options.restitution`

The root link's GameObject SHALL be parented to `root_parent` (when non-null) and its local `Transform` SHALL be set from `options.position` and `options.rotation`.

When `options.with_visuals` is false, the system SHALL NOT create any `StaticMeshComponent` or visual child GameObjects.

#### Scenario: Friction and restitution applied to all bodies
- **WHEN** the A1 robot is built with options `{static_friction = 0.8, dynamic_friction = 0.6, restitution = 0.2}`
- **THEN** every `RigidBodyComponent` in the hierarchy has `m_static_friction = 0.8`
- **AND** `m_dynamic_friction = 0.6`
- **AND** `m_restitution = 0.2`

#### Scenario: Root link placed by options
- **WHEN** the A1 robot is built with options `{position = (1, 2, 3), rotation = identity}` and a non-null `root_parent`
- **THEN** the `base` GameObject (root link) is a child of `root_parent`
- **AND** its local position is `(1, 2, 3)`

#### Scenario: Visuals skipped when disabled
- **WHEN** the A1 robot is built with `with_visuals = false`
- **THEN** no `StaticMeshComponent` exists in the built hierarchy

### Requirement: Build GameObject hierarchy from URDF link tree

The system SHALL build a `Scene` containing one `GameObject` per URDF link, arranged in a parent-child tree matching the joint structure.

The root link's GameObject SHALL be placed according to the build options: parented to the caller-provided `root_parent` (when non-null) with its local `Transform` set from `options.position` and `options.rotation`. With default options and no root parent, the root link sits at the world origin. Each non-root link's `GameObject` SHALL have its local `Transform` set from the incoming joint's `<origin>` tag, converted from URDF coordinate system (X=forward, Y=left, Z=up) to engine coordinate system (X=right, Y=forward, Z=up) using the conversion: `engine_pos = (-urdf.y, urdf.x, urdf.z)`.

The parent-child relationship SHALL be set via `child.SetParent(parent.GetHandle())` where parent is the GameObject of the joint's parent link.

#### Scenario: A1 trunk is child of base
- **WHEN** the A1 GameObject hierarchy is built
- **THEN** the `trunk` GameObject has `base` as its parent
- **AND** the `trunk` local position in engine coordinates is (0, 0, 0) because `floating_base` joint has origin (0, 0, 0)

#### Scenario: FR_hip is positioned correctly relative to trunk
- **WHEN** the A1 GameObject hierarchy is built
- **THEN** the `FR_hip` GameObject has `trunk` as its parent
- **AND** its local position in engine coordinates is approximately (0.047, 0.1805, 0) converted from URDF origin `(0.1805, -0.047, 0)`

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

### Requirement: Every URDF link gets an independent GameObject

Every URDF link SHALL be represented by exactly one GameObject, regardless of its joint type or whether it has collision, visual, or inertial elements. This includes:

- Links connected by `fixed` joints (e.g., `thigh_shoulder`, `imu_link`, foot links)
- Links with no `<inertial>` element (collision-only or visual-only links)
- Links with no `<visual>` or `<collision>` elements (inertial-only links)

The GameObject hierarchy SHALL exactly mirror the URDF link/joint tree — no links shall be merged or skipped.

#### Scenario: Collision-only link gets its own GO
- **WHEN** the A1 hierarchy is built
- **THEN** `FR_thigh_shoulder` exists as an independent GameObject
- **AND** it is a child of `FR_hip` (via the `FR_hip_fixed` fixed joint)
- **AND** it has no `RigidBodyComponent` (no `<inertial>`)

#### Scenario: Fixed-joint link gets its own GO
- **WHEN** the A1 hierarchy is built
- **THEN** `FR_foot` exists as an independent GameObject
- **AND** it is a child of `FR_calf` (via the `FR_foot_fixed` fixed joint)
- **AND** it has a `RigidBodyComponent` (has `<inertial>`)
- **AND** `FR_foot` has a `PhysicsConstraintComponent` with a `FixedJointDef` referencing `FR_calf`

### Requirement: Parent-child collision pairs are filtered

For each URDF joint connecting a parent link to a child link, the system SHALL populate `m_ignore_collision_shapes` on every `CollisionShapeComponent` in the child link's subtree with the `ComponentHandle` of every `CollisionShapeComponent` in the parent link's subtree, collected via `CollectCollisionComponentHandles` (depth-independent subtree traversal, all shapes per GameObject). The reverse direction (parent ignoring child) SHALL NOT be applied — child→parent filtering alone is sufficient because GPU filters are symmetric.

This prevents the physics solver from generating contact constraints between adjacent links that are physically connected by a joint.

#### Scenario: Child ignores parent collisions
- **WHEN** the A1 hierarchy is built
- **THEN** every `CollisionShapeComponent` on `FR_thigh` has `m_ignore_collision_shapes` containing all `CollisionShapeComponent` handles in the `FR_hip` subtree

#### Scenario: Parent does not declare child ignores
- **WHEN** the A1 hierarchy is built
- **THEN** `CollisionShapeComponent`s on `FR_hip` do not declare `FR_thigh` subtree shapes in `m_ignore_collision_shapes` (symmetry is enforced by the GPU filter allocation)

#### Scenario: Non-adjacent links can still collide
- **WHEN** the A1 hierarchy is built
- **THEN** `FL_thigh` and `FR_thigh` do NOT ignore each other's collisions (they are not connected by a joint — self-collision between legs is allowed)

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

- **WHEN** the A1 hierarchy is built with a joint connecting `FR_hip` (has inertial) to `FR_thigh_shoulder` (no inertial)
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

### Requirement: Save robot as SceneAsset prefab

After building the complete GameObject hierarchy, the system SHALL call `FlushCmdQueue()` on the temporary scene, create a `SceneAsset` via `SaveFromScene()`, and persist it to disk using `SaveAsset()` with name `"GO_" + robot_name`.

The resulting `.asset` file SHALL be loadable via `SceneAsset::AddToScene()` into any scene with physics enabled.

#### Scenario: Save A1 produces valid SceneAsset
- **WHEN** the A1 URDF import completes
- **THEN** a `.asset` file named `GO_a1.asset` exists at the import path
- **AND** loading it via `SceneAsset::AddToScene()` into the main scene creates 22 GameObjects with functioning rigid bodies, collision shapes, and joint constraints

### Requirement: Coordinate system conversion

All 3D quantities extracted from URDF SHALL be converted from the URDF coordinate system (X=forward, Y=left, Z=up) to the engine coordinate system (X=right, Y=forward, Z=up) using the following transformations:

- Position vectors: `engine = (-urdf.y, urdf.x, urdf.z)`
- Direction/axis vectors: same formula as position vectors
- Rotation (RPY): convert RPY to quaternion in URDF frame, then conjugate by the basis-change quaternion `angleAxis(+90°, Z)`

#### Scenario: URDF forward becomes engine forward
- **WHEN** a URDF position `(0.1805, -0.047, 0)` (forward-right) is converted
- **THEN** the engine position is `(0.047, 0.1805, 0)` (right-forward)

#### Scenario: URDF axis becomes engine axis
- **WHEN** a URDF axis `(0, 1, 0)` (left direction) is converted
- **THEN** the engine axis is `(-1, 0, 0)` (left direction)

### Requirement: UrdfBuiltJoint carries the converted hinge axis

`UrdfBuiltJoint` SHALL contain an `axis` field holding the hinge axis of the corresponding URDF joint, converted to engine coordinates in the parent link's GO frame via the same conversion used for the `HingeJointDef` (`UrdfAxisToEngine(joint.axis)`).

The field SHALL be populated for joints that produced a physical `HingeJointDef` (revolute/continuous with rigid bodies on both ends). For fixed joints or joints without a physical constraint, the field SHALL be left at its default (identity-like, e.g. the `UrdfJoint.axis` default) and consumers SHALL ignore it.

This makes the joint axis available to consumers of `BuildRobotScene` (e.g. `PhysicsApp`) without re-implementing the URDF coordinate conversion.

#### Scenario: A1 hinge joint exposes the engine axis

- **WHEN** the A1 hierarchy is built and `FR_thigh_joint`'s `UrdfBuiltJoint` entry is inspected
- **THEN** its `axis` is approximately `(-1, 0, 0)` in engine coordinates (converted from URDF axis `0 1 0`), matching the `HingeJointDef.m_hinge_axis_obj1` of the corresponding constraint
