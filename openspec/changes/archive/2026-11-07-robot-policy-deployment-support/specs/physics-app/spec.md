# physics-app Delta

## MODIFIED Requirements

### Requirement: Create performs full initialization

`PhysicsApp::Create(CreateInfo)` SHALL perform full initialization internally: `MainClass::Initialize`, `LoadBuiltinAssets`, and `LoadProject` (the built-in default project, which ships without skybox texture data). Asset and project paths SHALL be read from `cmake_config.h` macros (`ENGINE_BUILTIN_ASSETS_DIR`, `ENGINE_PROJECTS_DIR`) for this iteration. `CreateInfo` SHALL carry execution mode, window resolution, window title, a background clear color, and physics timing knobs: `physics_time_step` (seconds per `Step`, `0` keeps the engine default of 1/60 s), `physics_substeps` (`0` keeps the default), and `physics_iterations` (`0` keeps the default). Initialization failure SHALL throw.

The physics timing knobs SHALL reach the solver's configuration at creation time only — the engine's startup options and main-class state SHALL NOT carry physics fields.

#### Scenario: Consumer does not touch MainClass

- **WHEN** a consumer calls `PhysicsApp::Create`
- **THEN** no MainClass, asset database, or project loading calls appear in consumer code

#### Scenario: Paths come from cmake_config

- **WHEN** `Create` is called
- **THEN** builtin assets and project directories are taken from `ENGINE_BUILTIN_ASSETS_DIR` and `ENGINE_PROJECTS_DIR` macros

#### Scenario: Physics timing knobs reach the solver

- **WHEN** `Create` is called with `physics_time_step = 0.005` and the scene is committed and stepped
- **THEN** each `Step` advances the simulation by 0.005 s

#### Scenario: Zero knobs keep engine defaults

- **WHEN** `Create` is called with the physics timing knobs at their zero defaults
- **THEN** the solver uses the engine's default time step, substep count, and iteration count

### Requirement: Step executes one pure physics step

`Step` SHALL advance the physics simulation by one fixed step using a dedicated command buffer, with device-level waitIdle before and after the physics submission. The step time SHALL be the `physics_time_step` given at `Create` (engine default 1/60 s when zero). `Step` SHALL NOT process input events. Consecutive `Step` calls SHALL be allowed (physics fast-forward without rendering). While paused, `Step` SHALL remain callable and the solver layer SHALL no-op.

Because the step no longer produces model matrices as a side effect, `Step` SHALL also produce model matrices into the render system's buffer on the same command buffer after the step, so that a render frame following a step observes the poses that step produced.

#### Scenario: Step is repeatable

- **WHEN** `Step` is called twice in a row without `RenderNextFrame`
- **THEN** the physics simulation advances two steps without error

#### Scenario: Paused step does not evolve physics

- **WHEN** `Step` is called while paused
- **THEN** the physics state does not change

#### Scenario: A step refreshes model matrices

- **WHEN** `Step` advances the simulation and `RenderNextFrame` is called afterwards
- **THEN** the model matrices read by the render frame correspond to the poses the step produced
- **AND** the model matrices were recorded on the same command buffer as the step

#### Scenario: Configured time step is used

- **WHEN** the app was created with `physics_time_step = 0.005` and `Step` is called
- **THEN** the simulation advances by 0.005 s rather than the 1/60 s default
