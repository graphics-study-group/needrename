## MODIFIED Requirements

### Requirement: Create performs full initialization
`PhysicsApp::Create(CreateInfo)` SHALL perform full initialization internally: `MainClass::Initialize`, `LoadBuiltinAssets`, and `LoadProject` (the built-in default project, which ships without skybox texture data). Asset and project paths SHALL be read from `cmake_config.h` macros (`ENGINE_BUILTIN_ASSETS_DIR`, `ENGINE_PROJECTS_DIR`) for this iteration. `CreateInfo` SHALL carry execution mode, window resolution, window title, and a background clear color. Initialization failure SHALL throw.

#### Scenario: Consumer does not touch MainClass
- **WHEN** a consumer calls `PhysicsApp::Create`
- **THEN** no MainClass, asset database, or project loading calls appear in consumer code

#### Scenario: Paths come from cmake_config
- **WHEN** `Create` is called
- **THEN** builtin assets and project directories are taken from `ENGINE_BUILTIN_ASSETS_DIR` and `ENGINE_PROJECTS_DIR` macros

## ADDED Requirements

### Requirement: Background clear color instead of skybox
`CreateInfo` SHALL carry a background color (linear rgb, default black). The
rendered frame background SHALL be that solid color: no skybox material or
cubemap texture SHALL be drawn, and the built-in default project SHALL contain
no skybox material reference or cubemap texture data. Lighting of scene objects
SHALL remain driven by the directional light API and shadows, unaffected by the
background.

#### Scenario: Default background is black
- **WHEN** an app is created without setting the background color and an
  offscreen frame of an empty scene is read back
- **THEN** pixels not covered by geometry are black

#### Scenario: Configured background color renders
- **WHEN** an app is created with a non-black background color and an offscreen
  frame is read back
- **THEN** pixels not covered by geometry match the configured color (within
  tonemapping tolerance)

#### Scenario: Default project ships no skybox data
- **WHEN** the shipped default project directory is inspected
- **THEN** it contains no cubemap texture files and its level asset references
  no skybox material
