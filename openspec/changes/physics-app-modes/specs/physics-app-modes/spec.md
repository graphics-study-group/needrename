# physics-app-modes

## Purpose

Extends `PhysicsApp` from a single windowed form into three executable modes (headless physics-only, offscreen render-to-CPU-texture, windowed) and adds CPU readback of both physics rigid-body state and rendered pixels, enabling headless CI / RL / test use without changing the physics engine internals.

## Requirements

### Requirement: AppMode selects the execution form and is immutable

`CreateInfo` SHALL carry an `AppMode` enum with exactly three values — `Headless` (physics only: no rendering, no window, no camera), `Offscreen` (physics + render into an internal CPU-accessible texture, no window), and `Windowed` (physics + render + window, the pre-existing behavior). It SHALL default to `Windowed`. The mode SHALL be fixed at `Create` time and SHALL NOT change afterwards.

#### Scenario: Default mode is Windowed

- **WHEN** `PhysicsApp::Create(info)` is called with an `info.mode` left at its default
- **THEN** the app runs in windowed mode (window created, rendering enabled)

#### Scenario: Headless mode creates no window

- **WHEN** `PhysicsApp::Create` is called with `info.mode == AppMode::Headless`
- **THEN** no `SDLWindow`, `GUISystem`, or `Input` is created
- **AND** `MainClass` runs with the headless startup option

#### Scenario: Offscreen mode creates no window but renders

- **WHEN** `PhysicsApp::Create` is called with `info.mode == AppMode::Offscreen`
- **THEN** no window is created
- **AND** the render system runs with an offscreen present provider at the `CreateInfo` resolution

#### Scenario: Mode is immutable after create

- **WHEN** any code attempts to change the mode after `Create` returns
- **THEN** no such API exists; the mode is fixed for the app's lifetime

### Requirement: Mode 1 (Headless) skips camera, lights, and render graph

In `AppMode::Headless`, the scene builder SHALL create physics bodies only (no mesh visualization children, no color material resolution), the app SHALL NOT create the internal camera or set an active camera, and `CommitScene` SHALL NOT build the default render graph nor forward the physics model-matrices buffer to the render system. The scene root SHALL still be created for physics parenting.

#### Scenario: Physics bodies are created without visuals

- **WHEN** `AddBox` is called in headless mode
- **THEN** a rigid body and collision shape are created
- **AND** no mesh child GameObject and no material AssetRef are created

#### Scenario: No camera is created

- **WHEN** a headless app is created
- **THEN** no camera GameObject, CameraComponent, or CameraControllerComponent exists
- **AND** `ShouldQuit` remains callable and returns `false`

#### Scenario: CommitScene skips the render graph

- **WHEN** `CommitScene` is called in headless mode
- **THEN** physics GPU buffers are synced and the scene is frozen
- **AND** no default render graph is built

#### Scenario: Render-only APIs throw in headless mode

- **WHEN** `RenderNextFrame`, `GetRenderOutput`, `SetRenderReadbackEnabled(true)`, `AddDirectionalLight`, or `SetCameraPose` is called in headless mode
- **THEN** a `std::logic_error` is thrown

### Requirement: BodyId maps to rigid body slot indices after commit

`BodyId` SHALL be the creation-order index of a body (as today). After `CommitScene` (once component `Awake` has assigned rigid-body slots), a mapping from `BodyId` to the `PhysicsScene` rigid-body slot index SHALL be built and be available to consumers, so state arrays can be indexed by slot index.

#### Scenario: Mapping is built at commit

- **WHEN** `CommitScene` has completed for a scene with N bodies
- **THEN** the mapping has N entries, one per `BodyId`
- **AND** each entry equals the body's rigid-body slot index (from `PhysicsAdaptor::FindRigidBodyByObjectHandle`)

#### Scenario: Mapping entries are unique

- **WHEN** the mapping is inspected
- **THEN** all slot indices are distinct and within the scene's rigid-body slot range

### Requirement: Physics state readback API

A `GetBodyState(BodyId)` SHALL return a single body's `{position, rotation, linear_velocity, angular_velocity}` assembled from the read-back state arrays. A `GetBodyStates()` SHALL return a `BodyStatesView` containing the body→slot mapping (`slot_indices`), per-body center-of-mass offsets (`com_offsets`, GO-local space, static), and SoA spans for `positions`, `rotations` (quaternion xyzw), `linear_velocities`, and `angular_velocities` — all indexed by slot index. State SHALL reflect the last `Step()` for dynamic data and the committed initial state before the first `Step`. Reading before `CommitScene` SHALL throw `std::logic_error`; an invalid `BodyId` SHALL throw `std::out_of_range`.

#### Scenario: Initial state is readable before first step

- **WHEN** a body was created with a given position and `CommitScene` has completed but `Step` has not yet been called
- **THEN** `GetBodyState(id)` returns that exact position, an identity rotation, and zero velocities

#### Scenario: State advances with steps

- **WHEN** a dynamic body is simulated for N steps under gravity
- **THEN** `GetBodyState(id).position` reflects the simulated fall (vertical coordinate decreased from its initial value)

#### Scenario: COM offsets are exposed

- **WHEN** `GetBodyStates()` is called
- **THEN** `com_offsets` contains one element per rigid-body slot (zero for bodies whose COM is at the object origin, non-zero for imported bodies with an offset)

#### Scenario: Read before commit throws

- **WHEN** `GetBodyState` or `GetBodyStates` is called before `CommitScene`
- **THEN** a `std::logic_error` is thrown

#### Scenario: Invalid BodyId throws

- **WHEN** `GetBodyState` is called with `INVALID_BODY_ID` or an out-of-range id
- **THEN** a `std::out_of_range` is thrown

### Requirement: Opt-in render readback with blocking frame access

`SetRenderReadbackEnabled(bool)` SHALL toggle recording of a CPU readback copy of the final render target; it SHALL default to disabled. When enabled, each `RenderNextFrame` SHALL copy the final RTT into a resident host-visible staging buffer (rebuilt when the present extent changes), and `GetRenderOutput()` SHALL block until the most recently submitted frame completes, then return a `RenderOutput { const void* pixels, uint32_t width, uint32_t height, uint64_t frame_id }` where `pixels` is tight-packed RGBA8, row-major, top-to-bottom, valid until the next `RenderNextFrame`. `GetRenderOutput()` before any frame was captured SHALL throw `std::logic_error`; calling it while disabled SHALL throw `std::logic_error`.

#### Scenario: Readback is off by default

- **WHEN** a windowed app is created and committed with no readback call
- **THEN** no readback staging buffer is allocated and no copy is recorded

#### Scenario: Enabling readback captures rendered pixels

- **WHEN** `SetRenderReadbackEnabled(true)` is called in windowed or offscreen mode and `RenderNextFrame` runs once
- **THEN** `GetRenderOutput()` returns non-null pixels at the present extent with a monotonically increasing `frame_id`
- **AND** the pixels are not all zero (the scene rendered on top of the sky)

#### Scenario: GetRenderOutput before capture throws

- **WHEN** `SetRenderReadbackEnabled(true)` is called but no `RenderNextFrame` has run since
- **THEN** `GetRenderOutput()` throws `std::logic_error`

#### Scenario: GetRenderOutput while disabled throws

- **WHEN** `GetRenderOutput()` is called before `SetRenderReadbackEnabled(true)`
- **THEN** a `std::logic_error` is thrown

#### Scenario: Skipped frame returns previous capture

- **WHEN** a `RenderNextFrame` is skipped (e.g. swapchain out of date) after at least one successful capture
- **THEN** `GetRenderOutput()` returns the previous successful frame's pixels and `frame_id`

### Requirement: Frame flow and ShouldQuit vary by mode

`RenderNextFrame` SHALL throw `std::logic_error` in headless mode. In offscreen mode it SHALL run the render frame while skipping window input processing (SDL event polling, input updates, SPACE toggle) and no-quit semantics. In windowed mode it SHALL retain current behavior including input processing. `ShouldQuit` SHALL return `false` in headless and offscreen modes (no window close event source).

#### Scenario: RenderNextFrame throws in headless mode

- **WHEN** `RenderNextFrame` is called on a headless app
- **THEN** `std::logic_error` is thrown

#### Scenario: Offscreen frame skips input

- **WHEN** `RenderNextFrame` is called in offscreen mode
- **THEN** the render frame runs (timing, tick, render graph, readback, complete)
- **AND** SDL events and input state are not processed

#### Scenario: ShouldQuit is always false headlessly

- **WHEN** `ShouldQuit` is called on a headless or offscreen app
- **THEN** it returns `false` regardless of elapsed frames

### Requirement: Example replaced by mode test executables

`example/physics_example` SHALL be removed and its scene builders (template scenes, double pendulum) SHALL move into `test/physics_app_windowed_test.cpp`. Three test executables SHALL exist — `physics_app_headless_test`, `physics_app_offscreen_test`, `physics_app_windowed_test` — each linking `Engine` and `PhysicsApp`, and each registered with ctest. The windowed test SHALL accept an optional frame-count argument: with an argument it runs that many frames and resumes the simulation automatically after commit; without an argument it runs indefinitely starting paused (SPACE to resume), matching the former example UX.

#### Scenario: Headless test covers physics readback

- **WHEN** `physics_app_headless_test` runs
- **THEN** it builds bodies, commits, reads initial state, steps, and asserts the state mapping and readback behave per the physics readback requirement

#### Scenario: Offscreen test covers render readback

- **WHEN** `physics_app_offscreen_test` runs
- **THEN** it enables readback, renders, and asserts `GetRenderOutput` dimensions and frame progression

#### Scenario: Windowed test supports finite and manual runs

- **WHEN** `physics_app_windowed_test 120` is invoked by ctest
- **THEN** it runs 120 frames then exits, resuming the simulation immediately after commit
- **WHEN** it is invoked without arguments
- **THEN** it runs until the window is closed, starting paused (SPACE resumes)
