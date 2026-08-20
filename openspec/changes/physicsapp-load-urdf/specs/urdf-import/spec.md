# urdf-import (delta)

## ADDED Requirements

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

## MODIFIED Requirements

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
