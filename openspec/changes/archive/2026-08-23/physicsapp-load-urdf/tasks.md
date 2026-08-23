# Tasks: PhysicsApp URDF Loading

## 1. Framework: URDF builder extraction

- [x] 1.1 Add `UrdfBuildOptions`, `UrdfBuiltJoint`, `UrdfBuiltRobot` to `engine/Framework/Import/UrdfTypes.h` (FRAMEWORK_API structs, matching existing style)
- [x] 1.2 Declare public `ParseUrdf` and `BuildRobotScene` in `engine/Framework/Import/UrdfLoader.h` (doxygen: @brief/@param/@return, no-flush/no-save contract, map semantics)
- [x] 1.3 Implement `BuildRobotScene` by moving the hierarchy construction (current steps 1–7 of `BuildAndSaveSceneAsset`) into it: build into the caller `Scene`, parent root link to `root_parent`, apply `options` (placement on root link, friction/restitution on every `RigidBodyComponent`, `with_visuals` gate on the visual step), use member `m_database`/`m_asset_manager`
- [x] 1.4 Collect `link_objects` (only links that received a `RigidBodyComponent`) and `joint_objects` (only joints that produced a constraint; URDF parent/child order) while building
- [x] 1.5 Rewire `BuildAndSaveSceneAsset` as: temporary scene → `BuildRobotScene` (null root parent, default options, discard maps) → `FlushCmdQueue` → `SaveFromScene` → `SaveAsset`; keep `LoadUrdfResource` behavior identical
- [x] 1.6 Build and run the existing `urdf-import` related tests to confirm the save-to-disk path is unchanged

## 2. AppPhysics: LoadUrdf API

- [x] 2.1 Add `UrdfImportConfig`, `JointBodyPair`, `UrdfImportResult` to `app/physics/PhysicsApp.h` (glm/std types only — no Framework includes) and declare `UrdfImportResult LoadUrdf(const UrdfImportConfig &cfg)`
- [x] 2.2 Add `SceneBuilder::RegisterExistingBody(Engine::ObjectHandle)` (append to `m_bodies`, return new `BodyId`)
- [x] 2.3 Implement `PhysicsApp::LoadUrdf` in `PhysicsApp.cpp`: phase guard (logic_error), file existence + parse + empty-robot check (runtime_error), `with_visuals &= mode != PhysicsOnly`, translate config → `UrdfBuildOptions`, call `BuildRobotScene` into the main scene under the app root
- [x] 2.4 Build the `ObjectHandle → BodyId` lookup via `RegisterExistingBody`, assemble `UrdfImportResult` (joint endpoints resolved through the lookup)
- [x] 2.5 Verify URDF bodies are readable through the existing readback: build, run `physics_app_physics_only_test` / offscreen test to confirm `CommitScene` still passes with the new code path compiled in

## 3. Windowed test scene

- [x] 3.1 Add `AddUrdfRobotScene(PhysicsApp&)` to `test/physics_app_windowed_test.cpp`: `LoadUrdf` with `assets/a1_description/urdf/a1.urdf`, position ≈ (6, 0, 1.5) inside the arena, identity rotation, default friction/restitution
- [x] 3.2 Assert the returned maps in the test: `trunk`/`FR_thigh` present in `link_bodies`, `base`/`FR_thigh_shoulder` absent, `FR_thigh_joint` present with parent `FR_hip` and child `FR_thigh`, `floating_base` absent
- [x] 3.3 Call `AddUrdfRobotScene` from `main` alongside `AddTemplateScene2` (keep `AddTemplateScene` commented out); camera pose adjusted to frame both if needed
- [x] 3.4 Run the windowed test under ctest with a finite frame count and confirm the robot falls onto the ground without crashes or validation errors

## 4. Verification

- [x] 4.1 Configure + full build (`cmake --build --preset debug`) with the MSYS2 environment variables
- [x] 4.2 Run `ctest --preset debug` — all three PhysicsApp mode tests plus the windowed URDF scene pass
- [x] 4.3 Run `openspec validate physicsapp-load-urdf --strict` (if available) to confirm artifact integrity
