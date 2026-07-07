# urdf-import

## Purpose

Enable importing URDF (Universal Robot Description Format) XML files into the engine as `SceneAsset` prefabs, producing a complete GameObject hierarchy with rigid bodies, collision shapes, joint constraints, and visual meshes.

## ADDED Requirements

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

### Requirement: Build GameObject hierarchy from URDF link tree

The system SHALL build a `Scene` containing one `GameObject` per URDF link, arranged in a parent-child tree matching the joint structure.

The root link(s) SHALL be placed at world origin. Each non-root link's `GameObject` SHALL have its local `Transform` set from the incoming joint's `<origin>` tag, converted from URDF coordinate system (X=forward, Y=left, Z=up) to engine coordinate system (X=right, Y=forward, Z=up) using the conversion: `engine_pos = (-urdf.y, urdf.x, urdf.z)`.

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
- `m_use_manual_inertia` set to `true`
- `m_manual_inertia_diag` set to (ixx, iyy, izz)
- `m_manual_inertia_offdiag` set to (ixy, ixz, iyz)

Links without `<inertial>` SHALL NOT receive a `RigidBodyComponent`.

#### Scenario: A1 trunk gets rigid body with manual inertia
- **WHEN** the A1 hierarchy is built
- **THEN** the `trunk` GameObject has a `RigidBodyComponent`
- **AND** `m_mass` is `6.0f`
- **AND** `m_use_manual_inertia` is `true`
- **AND** `m_manual_inertia_diag` is approximately (0.0158533, 0.0377999, 0.0456542)

#### Scenario: A1 base link has no rigid body
- **WHEN** the A1 hierarchy is built
- **THEN** the `base` GameObject has no `RigidBodyComponent` because it has no `<inertial>` element

### Requirement: Attach collision shape components

For each URDF `<collision>` element, the system SHALL add a `CollisionShapeComponent` with:
- `m_shape_type` mapped from URDF geometry type (box → Box, sphere → Sphere, cylinder → Cylinder)
- `m_feature` set to appropriate half-extents/radius (box: half of full size, sphere: radius, cylinder: (radius, half_length))
- `m_center` set from the collision origin's xyz, converted to engine coordinates
- `m_rotation` set from the collision origin's rpy, converted to engine quaternion

Collision shapes SHALL be placed on child GameObjects when the collision origin is non-identity, so their world-space position accounts for both the link transform and the collision offset.

#### Scenario: A1 trunk collision is a box
- **WHEN** the A1 hierarchy is built
- **THEN** the `trunk` GameObject has at least one `CollisionShapeComponent` with type `Box`
- **AND** its `m_feature` is approximately (0.1335, 0.097, 0.057) — half of size (0.267, 0.194, 0.114)

#### Scenario: A1 foot collision is a sphere
- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_foot` GameObject has a `CollisionShapeComponent` with type `Sphere`
- **AND** its `m_feature.x` (radius) is `0.02f`

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
- **AND** `FR_calf` has a `PhysicsConstraintComponent` with a `FixedJointDef` referencing `FR_foot`

### Requirement: Parent-child collision pairs are filtered

For each URDF joint connecting a parent link to a child link, the system SHALL populate `m_ignore_collision_objects` on every `CollisionShapeComponent` in the child link's subtree with the ObjectHandle of every GameObject in the parent link's subtree that carries a `CollisionShapeComponent`. The reverse direction (parent ignoring child) SHALL also be applied for bidirectionality.

This prevents the physics solver from generating contact constraints between adjacent links that are physically connected by a joint.

#### Scenario: Child ignores parent collisions
- **WHEN** the A1 hierarchy is built
- **THEN** every `CollisionShapeComponent` on `FR_thigh` has `m_ignore_collision_objects` containing all GameObjects in the `FR_hip` subtree that carry a `CollisionShapeComponent`

#### Scenario: Parent ignores child collisions
- **WHEN** the A1 hierarchy is built
- **THEN** every `CollisionShapeComponent` on `FR_hip` has `m_ignore_collision_objects` containing all GameObjects in the `FR_thigh` subtree that carry a `CollisionShapeComponent`

#### Scenario: Non-adjacent links can still collide
- **WHEN** the A1 hierarchy is built
- **THEN** `FL_thigh` and `FR_thigh` do NOT ignore each other's collisions (they are not connected by a joint — self-collision between legs is allowed)

### Requirement: Attach joint constraint components

For each URDF joint with type `fixed` or `revolute`, the system SHALL add a `PhysicsConstraintComponent` to the parent link's GameObject.

For `fixed` joints, a `FixedJointDef` SHALL be created with:
- `m_obj2_handle` referencing the child link's GameObject
- `m_compliance` set to `0.0f`

For `revolute` joints, a `HingeJointDef` SHALL be created with:
- `m_obj2_handle` referencing the child link's GameObject
- `m_obj1_local_aligned_axis` and `m_obj2_local_aligned_axis` set to the URDF axis converted to engine coordinates (assumed identity rotation between frames for Phase 1)
- `m_obj1_local_attach_point` set to the joint origin position in the parent's local frame (engine coordinates)
- `m_obj2_local_attach_point` set to zero (child COM at child GO origin)
- `m_compliance` set to `0.0f`

Joints of type `floating` or `prismatic` SHALL be skipped with a log warning.

#### Scenario: A1 revolute joints become hinge constraints
- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_hip` GameObject has a `PhysicsConstraintComponent` containing a `HingeJointDef`
- **AND** `m_obj2_handle` references the `FR_thigh` GameObject
- **AND** the aligned axis is approximately `(-1, 0, 0)` in engine coordinates (converted from URDF axis `0 1 0`)

#### Scenario: A1 fixed joints become fixed constraints
- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_calf` GameObject has a `PhysicsConstraintComponent` containing a `FixedJointDef`
- **AND** `m_obj2_handle` references the `FR_foot` GameObject

#### Scenario: A1 floating_base joint creates a fixed constraint
- **WHEN** the A1 hierarchy is built
- **THEN** the `base` GameObject has a `PhysicsConstraintComponent` containing a `FixedJointDef`
- **AND** `m_obj2_handle` references the `trunk` GameObject

### Requirement: Attach render mesh components from collision geometry using builtin assets

For each URDF `<collision>` element with a geometry type of box, sphere, or cylinder, the system SHALL add a `StaticMeshComponent` to the link's GameObject with `m_mesh_asset` referencing the corresponding builtin mesh (`~/mesh/cube.asset`, `~/mesh/sphere.asset`, or `~/mesh/cylinder.asset`).

The `m_material_assets` vector SHALL contain one reference to a PBR solid color material from the builtin materials, selected deterministically based on a hash of the link name.

Collision elements with a mesh geometry (referencing DAE/STL files) SHALL be skipped in Phase 1 with a log info message.

#### Scenario: A1 trunk render uses cube builtin mesh from collision
- **WHEN** the A1 hierarchy is built
- **THEN** the `trunk` GameObject has a `StaticMeshComponent`
- **AND** `m_mesh_asset` references a mesh with path `~/mesh/cube.asset`
- **AND** the mesh scale matches the trunk's collision box half-extents (0.1335, 0.097, 0.057)

#### Scenario: A1 foot render uses sphere builtin mesh from collision
- **WHEN** the A1 hierarchy is built
- **THEN** the `FR_foot` GameObject has a `StaticMeshComponent`  
- **AND** `m_mesh_asset` references a mesh with path `~/mesh/sphere.asset`
- **AND** the mesh scale matches the foot's collision sphere radius (0.02)

#### Scenario: Same link always gets same material color
- **WHEN** the A1 hierarchy is built twice
- **THEN** the `trunk` GameObject receives the same builtin material in both builds

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
