# Proposal: PhysicsApp URDF Loading

## Why

`PhysicsApp` will be shipped as a library, so URDF robots must be loadable directly into its running scene without creating asset files on disk. The existing `UrdfLoader::LoadUrdfResource` always builds into a temporary scene and persists a `SceneAsset`; there is no in-memory path, and no way for the caller to map URDF link/joint names to physics bodies — which is a prerequisite for the future force/joint-driving work.

## What Changes

- **New** `UrdfLoader::BuildRobotScene(robot, scene, root_parent, options)` public API: builds the full GameObject tree (rigid bodies, collision shapes, joint constraints, visuals) directly into a caller-provided `Scene`, without creating a temporary scene, flushing, or saving. Returns an `UrdfBuiltRobot` with `link name → ObjectHandle` and `joint name → {parent, child} ObjectHandle` maps.
- **New** `UrdfBuildOptions` (position, rotation, static/dynamic friction, restitution, with_visuals): overall robot placement, uniform friction/restitution applied to every rigid body, and a visuals on/off switch.
- **New** `UrdfLoader::ParseUrdf` becomes public (parse and build remain two steps).
- **Modified** `UrdfLoader::BuildAndSaveSceneAsset` becomes a thin wrapper: temporary scene + `BuildRobotScene` + flush + `SaveFromScene`. The existing save-to-disk path keeps its behavior.
- **New** `PhysicsApp::LoadUrdf(const UrdfImportConfig&)` (Building phase only) returning `UrdfImportResult`:
  - `link_bodies`: link name → `BodyId`, only links that got a `RigidBodyComponent` (have `<inertial>`).
  - `joint_bodies`: joint name → `{parent BodyId, child BodyId}`, only joints that produced a physical constraint (fixed/revolute/continuous with both ends having bodies).
  Visual meshes are decided by the app mode (`PhysicsOnly` = none, `Offscreen`/`Windowed` = yes), so `UrdfImportConfig` has no visuals switch.
- **New** `SceneBuilder::RegisterExistingBody(ObjectHandle)` to register URDF bodies into the BodyId registry so they work with `GetBodyState`/`GetBodyStates` unchanged.
- **New** windowed test scene: load the A1 URDF robot next to the existing `AddTemplateScene2` scene (ground already exists), free-falling onto it.
- **New** types `UrdfBuildOptions`, `UrdfBuiltJoint`, `UrdfBuiltRobot` (Framework), `UrdfImportConfig`, `UrdfImportResult`, `JointBodyPair` (AppPhysics).

No breaking changes: `LoadUrdfResource` keeps its signature and behavior.

## Capabilities

### New Capabilities
- `physicsapp-urdf-loading`: `PhysicsApp::LoadUrdf` API — config (path, placement, friction/restitution, visuals), the two returned maps and their semantics, phase rules, and error handling.

### Modified Capabilities
- `urdf-import`: the hierarchy-building logic becomes a reusable public `BuildRobotScene` targeting a caller-provided scene with configurable placement/friction/visuals and returns name→handle maps; the save-as-prefab path is reimplemented on top of it.

## Impact

- `engine/Framework/Import/UrdfLoader.h/.cpp` — extract `BuildRobotScene`, publicize `ParseUrdf`, rewire `BuildAndSaveSceneAsset`.
- `engine/Framework/Import/UrdfTypes.h` — add `UrdfBuildOptions`, `UrdfBuiltJoint`, `UrdfBuiltRobot` (or a new sibling header if preferred).
- `app/physics/PhysicsApp.h/.cpp` — `UrdfImportConfig`, `UrdfImportResult`, `JointBodyPair`, `LoadUrdf`.
- `app/physics/SceneBuilder.h/.cpp` — `RegisterExistingBody`.
- `test/physics_app_windowed_test.cpp` — URDF robot scene alongside `AddTemplateScene2`.
- No CMake changes: `PhysicsApp` already links `Engine`; `UrdfLoader` is already `FRAMEWORK_API`.
