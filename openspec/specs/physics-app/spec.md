# physics-app

## Purpose

Define the `PhysicsApp` DLL — a complete, minimal entry point for GPU physics simulation (build scene → commit → step/render) that hides engine internals (MainClass, World, Asset) from consumers and prepares the path toward C API / Python distribution.

## Requirements

### Requirement: PhysicsApp DLL and directory layout
The change SHALL add a `PhysicsApp` shared library in a new `app/physics/` directory inside the unified `app/` family. The top-level `editor/` directory SHALL also be relocated into `app/editor/` (pure move; `EngineEditor` target and `<Editor/...>` includes unchanged). The DLL SHALL be named `PhysicsApp` (without an `Engine` prefix).

#### Scenario: App family layout
- **WHEN** the change is implemented
- **THEN** `app/` contains both `app/physics/` (PhysicsApp sources) and `app/editor/` (relocated editor sources), and the `app/physics` CMake target produces a `PhysicsApp` DLL

#### Scenario: Editor relocation is behavior-preserving
- **WHEN** `editor/` is relocated to `app/editor/`
- **THEN** the `EngineEditor` target name and `<Editor/...>` includes remain unchanged and `editor_run_game_example` still builds and links

### Requirement: PhysicsApp is non-singleton
`PhysicsApp` SHALL be a non-singleton class with an explicit `Create` factory returning a unique-ownership instance. Multiple instances SHALL be constructible in principle (multi-scene rendering limitations of the main system are out of scope).

#### Scenario: Create returns owned instance
- **WHEN** `PhysicsApp::Create(info)` is called with valid create info
- **THEN** a unique-ownership `PhysicsApp` instance is returned

### Requirement: Create performs full initialization
`PhysicsApp::Create(CreateInfo)` SHALL perform full initialization internally: `MainClass::Initialize`, `LoadBuiltinAssets`, and `LoadProject` (project `empty_with_sky`). Asset and project paths SHALL be read from `cmake_config.h` macros (`ENGINE_BUILTIN_ASSETS_DIR`, `ENGINE_PROJECTS_DIR`) for this iteration. `CreateInfo` SHALL carry window resolution and window title only. Initialization failure SHALL throw.

#### Scenario: Consumer does not touch MainClass
- **WHEN** a consumer calls `PhysicsApp::Create`
- **THEN** no MainClass, asset database, or project loading calls appear in consumer code

#### Scenario: Paths come from cmake_config
- **WHEN** `Create` is called
- **THEN** builtin assets and project directories are taken from `ENGINE_BUILTIN_ASSETS_DIR` and `ENGINE_PROJECTS_DIR` macros

### Requirement: Two-phase state machine with scene freeze
`PhysicsApp` SHALL enforce a one-way two-phase state machine: `Building` (after Create) → `Committed` (after `CommitScene`). During Building, build APIs SHALL be legal and drive APIs (`Step`, `RenderNextFrame`, `Pause`, `Resume`) SHALL throw `std::logic_error`. After Commit, build APIs SHALL throw `std::logic_error` (scene freeze, preventing physics scene refresh), drive APIs SHALL be legal, and a second `CommitScene` SHALL throw `std::logic_error`. `ShouldQuit` and `SetCameraPose` SHALL be legal in both phases. Runtime controls (pause/resume, camera) SHALL NOT be considered scene changes.

#### Scenario: Build API after commit throws
- **WHEN** `AddBox` is called after `CommitScene`
- **THEN** a `std::logic_error` is thrown

#### Scenario: Drive API before commit throws
- **WHEN** `Step` is called before `CommitScene`
- **THEN** a `std::logic_error` is thrown

#### Scenario: Double commit throws
- **WHEN** `CommitScene` is called a second time
- **THEN** a `std::logic_error` is thrown

#### Scenario: Runtime controls survive freeze
- **WHEN** `Pause`, `Resume`, or `SetCameraPose` is called after `CommitScene`
- **THEN** no exception is thrown

### Requirement: Body creation API with opaque BodyId
`AddBox`, `AddSphere`, and `AddCylinder` SHALL return an opaque `BodyId` value type. The API SHALL NOT expose `GameObject`, `ObjectHandle`, or other World concepts to consumers.

#### Scenario: BodyId identifies the created body
- **WHEN** `AddBox(desc)` is called
- **THEN** the returned `BodyId` can later be passed to the joint APIs

### Requirement: Generic joint API
`AddFixedJoint(BodyId obj1, BodyId obj2, FixedJointParams)` and `AddHingeJoint(BodyId obj1, BodyId obj2, HingeJointParams)` SHALL create joints where obj1 is the owning body (mirroring `PhysicsConstraintComponent` semantics) and obj2 is the referenced body. `FixedJointParams` SHALL contain `compliance`; `HingeJointParams` SHALL contain `axis_obj1`, `anchor_obj1`, and `compliance`. Initial relative transforms SHALL be derived at Awake time from current world poses (existing component behavior).

#### Scenario: Fixed joint between two boxes
- **WHEN** `AddFixedJoint(box_a, box_b, {.compliance = 0.0f})` is called with valid BodyIds
- **THEN** a fixed joint is registered between the two bodies with hard-constraint compliance

#### Scenario: Hinge joint with local axis and anchor
- **WHEN** `AddHingeJoint(box_a, box_b, {.axis_obj1 = {0,1,0}, .anchor_obj1 = {0,0,0}})` is called
- **THEN** a hinge joint is registered using the obj1-local axis and anchor

### Requirement: Color string material resolution
Body descs SHALL carry a `color` string instead of a material AssetRef. Valid values SHALL be `red`, `green`, `blue`, `yellow`, `cyan`, `magenta`, `orange`, and `white`, each resolved to the corresponding `builtin://materials/solid_color_<color>.asset` material. An empty string SHALL select a random color from the eight at Add time. An invalid non-empty string SHALL throw `std::invalid_argument` during the build phase.

#### Scenario: Named color resolution
- **WHEN** `AddBox({.color = "red"})` is called
- **THEN** the created mesh uses the `solid_color_red` builtin material

#### Scenario: Empty color selects random builtin color
- **WHEN** `AddBox({})` is called with no color
- **THEN** a randomly chosen builtin solid color material is used

#### Scenario: Invalid color throws
- **WHEN** `AddBox({.color = "chartreuse"})` is called
- **THEN** `std::invalid_argument` is thrown

### Requirement: Directional light API
`AddDirectionalLight(DirectionalLightParams)` SHALL create a directional light with `direction` (vec3), `color` (vec3), `intensity`, and `cast_shadow` fields and SHALL return `void`. The light SHALL NOT be a physics body.

#### Scenario: Directional light creation
- **WHEN** `AddDirectionalLight({.direction = {0,0,-1}, .intensity = 2.0f})` is called
- **THEN** a directional light with intensity 2.0 is added to the scene

### Requirement: Internal camera with default pose and fly controls
The app SHALL manage its own camera internally (no consumer camera configuration) with fly controls (keyboard movement + mouse look, mirroring the former `CameraControllerComponent`). `SetCameraPose(position, look_target)` SHALL set the camera pose during Building (optional; a default pose looking at the world origin SHALL be used otherwise) and SHALL remain callable after Commit. The camera SHALL be excluded from scene freeze.

#### Scenario: Default camera pose
- **WHEN** the app is committed without calling `SetCameraPose`
- **THEN** a default camera pose looking toward the origin is used

#### Scenario: Camera pose adjustable after commit
- **WHEN** `SetCameraPose(...)` is called after `CommitScene`
- **THEN** the camera pose is updated without throwing

### Requirement: CommitScene performs one-way commit
`CommitScene` SHALL, in order: flush the command queue; add and process init events (component Init/Awake, joint setup); flush physics (adaptor conversion + GPU buffer sync); forward the physics model matrices buffer to the render system; build the default render graph (via `ComplexRenderGraphBuilder`); and set the freeze flag. After commit the simulation SHALL start paused.

#### Scenario: Commit sequence freezes and pauses
- **WHEN** `CommitScene` is called after building a scene
- **THEN** all pending scene work is flushed, the render graph is built, the scene is frozen, and the simulation is paused

### Requirement: Step executes one pure physics step
`Step` SHALL advance the physics simulation by one fixed step (XpbdConfig timestep 1/60) using a dedicated command buffer, with device-level waitIdle before and after the physics submission. `Step` SHALL NOT process input events. Consecutive `Step` calls SHALL be allowed (physics fast-forward without rendering). While paused, `Step` SHALL remain callable and the solver layer SHALL no-op.

#### Scenario: Step is repeatable
- **WHEN** `Step` is called twice in a row without `RenderNextFrame`
- **THEN** the physics simulation advances two steps without error

#### Scenario: Paused step does not evolve physics
- **WHEN** `Step` is called while paused
- **THEN** the physics state does not change

### Requirement: RenderNextFrame processes input and renders one frame
`RenderNextFrame` SHALL process SDL events and input updates, update the camera, update renderer data, and execute one render frame (StartFrame → record all render graph passes → CompleteFrame). If StartFrame reports a swapchain-out-of-date failure, the frame SHALL be skipped without throwing.

#### Scenario: Input processed inside RenderNextFrame
- **WHEN** `RenderNextFrame` is called
- **THEN** SDL events are polled and input state is updated within the call

### Requirement: Pause and Resume with built-in SPACE toggle
`Pause` and `Resume` SHALL wrap `PhysicsScene::SetSimulationEnabled`. The app SHALL internally handle the SPACE key as a pause/resume toggle. Rendering SHALL continue while paused.

#### Scenario: SPACE toggles simulation
- **WHEN** the SPACE key is pressed while the app is running
- **THEN** simulation toggles between paused and running without consumer code

### Requirement: ShouldQuit reflects SDL_QUIT only
`ShouldQuit` SHALL return true only when an SDL_QUIT event (window close) has been received. It SHALL be callable in any phase.

#### Scenario: Window close sets quit flag
- **WHEN** the window close (SDL_QUIT) event is received
- **THEN** subsequent `ShouldQuit` calls return true

### Requirement: Engine modules remain unchanged
The app SHALL be built exclusively on existing public interfaces (MainClass getters, PhysicsSystem, RenderSystem, PhysicsAdaptor). `MainClass::MainLoop` SHALL NOT be used. No changes to Physics, Render, Framework, or MainClass SHALL be required.

#### Scenario: App runs without MainLoop
- **WHEN** the app drives `Step`/`RenderNextFrame` in a loop
- **THEN** `MainClass::MainLoop` is never invoked

### Requirement: Thin physics example
`example/physics_example` SHALL be rewritten as a thin executable (~80 lines) that links `PhysicsApp`, configures scene content via the app build API, keeps both `AddTemplateScene` and `AddTemplateScene2` content functions (scene 2 commented out), rebuilds the double pendulum using the joint API, and drives a three-line loop (`while (!app->ShouldQuit()) { app->Step(); app->RenderNextFrame(); }`).

#### Scenario: Example main is thin
- **WHEN** the rewritten example is inspected
- **THEN** it contains no render graph construction, no camera construction, and no MainClass initialization calls
