# physicsapp-urdf-loading

## Purpose

Load URDF robot descriptions directly into a running `PhysicsApp` instance without creating asset files, and expose link/joint name → `BodyId` maps for future force-driving work.

## ADDED Requirements

### Requirement: LoadUrdf imports a URDF robot into the running app

`PhysicsApp` SHALL provide `LoadUrdf(const UrdfImportConfig&)` callable during the Building phase (before `CommitScene`).

`UrdfImportConfig` SHALL contain:
- `urdf_path`: filesystem path to the `.urdf` file
- `position`, `rotation`: overall placement of the robot (applied to the root link)
- `static_friction`, `dynamic_friction`, `restitution`: uniform coefficients applied to every created rigid body

The function SHALL parse the URDF file, build the robot hierarchy into the app's main scene with the app's root GameObject as `root_parent`, register every link body into the `BodyId` registry, and return an `UrdfImportResult`.

Visual meshes SHALL be built in `Offscreen` and `Windowed` modes and SHALL NOT be built in `PhysicsOnly` mode, decided by the app's mode (no config field).

#### Scenario: Load A1 returns maps
- **WHEN** `LoadUrdf` is called with the path to `a1.urdf`
- **THEN** the returned `UrdfImportResult.link_bodies` contains an entry for `trunk` mapped to a valid `BodyId`
- **AND** `joint_bodies` contains an entry for `FR_thigh_joint`
- **AND** the app does not write any `.asset` file for the robot

#### Scenario: Loaded robot free-falls after commit
- **WHEN** the A1 robot is loaded at a height above the ground and `CommitScene` is called
- **THEN** after several `Step` calls the robot has fallen (its trunk position's Z decreased from its initial value)

### Requirement: BodyId maps contain only physics-realized entries

`UrdfImportResult.link_bodies` SHALL map link name → `BodyId` ONLY for links that received a `RigidBodyComponent` (links with `<inertial>`).

`UrdfImportResult.joint_bodies` SHALL map joint name → `JointBodyPair { parent, child }` ONLY for joints that produced a physical constraint (fixed/revolute/continuous with rigid bodies on both ends). `parent` and `child` SHALL follow the URDF parent/child semantics: `parent` is the link closer to the root.

Calling `LoadUrdf` multiple times SHALL append bodies to the global `BodyId` registry so each call returns distinct `BodyId`s; link/joint name collisions across robots are the caller's responsibility.

#### Scenario: Links without inertial are absent
- **WHEN** the A1 robot is loaded
- **THEN** `link_bodies` does NOT contain `base` or `FR_thigh_shoulder` (both lack `<inertial>`)

#### Scenario: Joints without both ends are absent
- **WHEN** the A1 robot is loaded
- **THEN** `joint_bodies` does NOT contain `floating_base` (parent `base` has no body)
- **AND** `joint_bodies` does NOT contain `FR_hip_fixed` (child `FR_thigh_shoulder` has no body)

#### Scenario: Joint pair follows URDF parent/child order
- **WHEN** the A1 robot is loaded
- **THEN** `joint_bodies["FR_thigh_joint"].parent` equals `link_bodies["FR_hip"]`
- **AND** `joint_bodies["FR_thigh_joint"].child` equals `link_bodies["FR_thigh"]`

#### Scenario: Repeated loads give distinct BodyIds
- **WHEN** `LoadUrdf` is called twice for the same robot
- **THEN** every `BodyId` in the second result differs from every `BodyId` in the first result

### Requirement: URDF bodies integrate with the existing readback APIs

BodyIds returned by `LoadUrdf` SHALL be usable with `GetBodyState` and included in `GetBodyStates`, exactly like bodies created via `AddBox`/`AddSphere`/`AddCylinder`.

#### Scenario: GetBodyState works for a URDF link
- **WHEN** a robot is loaded, committed, and the simulation has not yet stepped
- **THEN** `GetBodyState(link_bodies["trunk"])` returns the trunk's initial center-of-mass position without throwing

### Requirement: LoadUrdf phase rules and error handling

`LoadUrdf` SHALL throw `std::logic_error` when called after `CommitScene` (scene is frozen).

`LoadUrdf` SHALL throw `std::runtime_error` when the file does not exist, the XML cannot be parsed, or the parsed robot has no links.

In `PhysicsOnly` mode, the robot SHALL be built physics-only (no visual meshes); `Offscreen` and `Windowed` modes build visual meshes. This is decided by the app's mode.

#### Scenario: Load after commit throws
- **WHEN** `LoadUrdf` is called after `CommitScene`
- **THEN** `std::logic_error` is thrown

#### Scenario: Missing file throws
- **WHEN** `LoadUrdf` is called with a path that does not exist
- **THEN** `std::runtime_error` is thrown

#### Scenario: PhysicsOnly builds no visual meshes
- **WHEN** `LoadUrdf` is called in `PhysicsOnly` mode
- **THEN** no error is thrown
- **AND** no visual meshes are created
