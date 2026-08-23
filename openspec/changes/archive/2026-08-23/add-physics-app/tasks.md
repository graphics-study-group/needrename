# Tasks: Add Physics App

## 1. Project Scaffolding

- [x] 1.1 Relocate `editor/` to `app/editor/`: move the directory, update `ENGINE_EDITOR_DIR` and `add_subdirectory(editor)` in root `CMakeLists.txt`
- [x] 1.2 Verify the `EngineEditor` target and `editor_run_game_example` still build and link unchanged (target names and `<Editor/...>` includes unaffected)
- [ ] 1.3 Create `app/physics/` directory with `CMakeLists.txt` building a `PhysicsApp` shared library linked against `Engine`, mirroring `app/editor/CMakeLists.txt` (no reflection parser, no export macros)
- [ ] 1.4 Register `add_subdirectory(app/physics)` in root `CMakeLists.txt`
- [ ] 1.5 Verify the project still configures and builds with `cmake --preset debug`

## 2. SceneBuilder Migration and Split

- [x] 2.1 Move `SceneBuilder` sources from `example/physics_example/` into `app/physics/` (namespace adjusted to app layer)
- [x] 2.2 Split `SceneBuilder` into physics assembly (RigidBodyComponent + CollisionShapeComponent + joints, no Asset dependency) and visualization assembly (StaticMeshComponent + builtin meshes + materials)
- [x] 2.3 Replace `material` (AssetRef) fields in `BoxDesc`/`SphereDesc`/`CylinderDesc` with `color` (string); empty string selects a random builtin color at Add time; invalid non-empty strings throw `std::invalid_argument`
- [x] 2.4 Change Add methods to return an opaque `BodyId` instead of `GameObject&`
- [x] 2.5 Make mesh component tracking internal (remove `GetMeshComponents()` from the public surface); remove `Finalize()` (work moves to `CommitScene`)
- [x] 2.6 Migrate `CameraControllerComponent` into the app as an internal camera control component; delete `SimulationToggleComponent` from the example

## 3. PhysicsApp Facade

- [x] 3.1 Implement `PhysicsApp` class with `CreateInfo` (resol_x, resol_y, window_title), non-singleton `Create` factory performing full initialization internally (MainClass Initialize, builtin assets, `empty_with_sky` project from cmake_config paths); hold `shared_ptr<MainClass>` for teardown order
- [x] 3.2 Implement the two-phase state machine (Building → Committed) with `std::logic_error` on wrong-phase build/drive calls and on double `CommitScene`; `ShouldQuit`/`SetCameraPose` legal in both phases
- [x] 3.3 Implement build API: `AddBox`/`AddSphere`/`AddCylinder` (BodyId), `AddFixedJoint`/`AddHingeJoint` (obj1 = owning body, params mirroring `FixedJointDef`/`HingeJointDef`), `AddDirectionalLight` (void, vec3 color), `SetCameraPose`
- [x] 3.4 Implement `CommitScene`: FlushCmdQueue → AddInitEvent + ProcessEvents → FlushPhysics → SetModelMatricesBuffer → BuildDefaultRenderGraph → freeze flag; simulation starts paused

## 4. Drive Phase

- [x] 4.1 Implement `Step`: device waitIdle → PreGPUStep → dedicated command buffer with GPUStep recorded via an app-owned `SubmissionHelper` → ExecuteSubmissionImmediately → PostGPUStep → device waitIdle; consecutive calls allowed
- [x] 4.2 Implement `Pause`/`Resume` wrapping `PhysicsScene::SetSimulationEnabled`; solver no-ops while paused
- [x] 4.3 Implement `RenderNextFrame`: SDL event poll + input update → camera update (fly controls, SPACE pause toggle) → UpdateRendererData → StartFrame (skip on swapchain-out-of-date) → RecordAllPasses → CompleteFrame
- [x] 4.4 Implement `ShouldQuit` reflecting SDL_QUIT only
- [x] 4.5 Ensure camera pose updates after commit work (camera excluded from freeze)

## 5. Thin Example

- [x] 5.1 Rewrite `example/physics_example/main.cpp` as a thin shell (~80 lines): Create → build content via app API → SetCameraPose → CommitScene → `while (!app->ShouldQuit()) { app->Step(); app->RenderNextFrame(); }`
- [x] 5.2 Keep `AddTemplateScene` and `AddTemplateScene2` as example-local content functions (scene 2 commented out); rebuild the double pendulum with `AddBox` + `AddHingeJoint` + `AddFixedJoint`; keep ground plane and directional lights
- [x] 5.3 Update `example/physics_example/CMakeLists.txt` to link `PhysicsApp` instead of `Engine`; remove migrated source files from the example target

## 6. Build and Verification

- [x] 6.1 Full build passes with `cmake --preset debug`
- [ ] 6.2 Manual run of `physics_example`: scene freezes after commit (post-commit build call throws), SPACE pause/resume works, camera fly controls work, double pendulum behaves correctly with joints, window close quits, consecutive `Step` fast-forward works
- [x] 6.3 Verify no engine-module files were modified (Physics/Render/Framework/MainClass untouched)
- [x] 6.4 Verify `editor` and `editor_run_game_example` behave unchanged after the relocation
