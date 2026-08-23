# Proposal: Add Physics App

## Why

The engine already has an app-layer DLL pattern (`EngineEditor`), but GPU physics simulation exists only as a 420-line bare `physics_example` main() that cannot be reused. This change introduces a `PhysicsApp` DLL — a complete, minimal entry point for physics simulation (build scene → commit → step/render) — so simulation becomes distributable and, in the future, wrappable as a C API or Python library without exposing engine internals to consumers.

## What Changes

- **New** `app/physics/` directory with a `PhysicsApp` DLL (no `Engine` prefix), as part of a unified `app/` app-family layout.
- **Moved** the top-level `editor/` directory into `app/editor/` (pure relocation: `EngineEditor` target, `<Editor/...>` includes, and behavior unchanged; only `ENGINE_EDITOR_DIR` and `add_subdirectory` in the root `CMakeLists.txt` move).
- **New** non-singleton `PhysicsApp` facade class:
  - `Create(...)` performs full initialization internally (`MainClass::Initialize`, builtin assets, project load; paths read from `cmake_config.h` for now).
  - Build-phase API: `AddBox` / `AddSphere` / `AddCylinder` returning opaque `BodyId`, `AddFixedJoint` / `AddHingeJoint` (generic joint API; no `AddDoublePendulum`), `AddDirectionalLight` (returns `void`), `SetCameraPose`.
  - Material selection simplified to color strings mapped to builtin `solid_color_*` materials; empty string selects a random color; invalid strings throw `std::invalid_argument` during build phase.
  - `CommitScene()`: one-way commit that runs all flush/init work, builds the default render graph, and freezes the scene (subsequent build calls throw `std::logic_error`). Simulation starts paused.
  - Drive-phase API: `Step()` (one pure physics step, fixed 1/60 timestep, dedicated command buffer with device-level waitIdle before and after, repeatable), `RenderNextFrame()` (event polling + input + camera update + render one frame), `Pause()` / `Resume()` (SPACE toggle built in), `ShouldQuit()` (SDL_QUIT only), `SetCameraPose` (also callable after commit).
  - Internal default camera with fly controls (migrated `CameraControllerComponent`), internal default render graph, internal solver-adjacent setup.
- **Modified** `SceneBuilder` from `physics_example` migrates into the app and is split into physics assembly and visualization assembly layers; `SimulationToggleComponent` is removed (SPACE pause is app built-in).
- **Modified** `example/physics_example` rewritten as a thin ~80-line executable that links `PhysicsApp` and only configures scene content (both template scene functions kept; scene 2 stays commented out).
- Engine modules (`Physics`, `Render`, `Framework`, `MainClass`) are unchanged; the app does not use `MainClass::MainLoop`.

## Capabilities

### New Capabilities

- `physics-app`: The `PhysicsApp` DLL facade — lifecycle, two-phase state machine with scene freeze, build API (bodies, joints, lights, camera), fixed-step drive API (Step/RenderNextFrame/Pause/Resume/ShouldQuit), internal camera/render-graph management, and error semantics (exceptions for invalid usage).

### Modified Capabilities

- `physics-scene-builder`: `SceneBuilder` moves from the physics example into the PhysicsApp DLL and is split into physics assembly and visualization assembly; `BoxDesc`/`SphereDesc`/`CylinderDesc` replace the `material` (AssetRef) field with a `color` string (empty = random builtin color); Add methods return an app-level opaque `BodyId` instead of `GameObject&`.

## Impact

- **New code**: `app/physics/` (PhysicsApp facade, split SceneBuilder, camera controller component), CMake wiring, DLL named `PhysicsApp`.
- **Moved code**: `editor/` → `app/editor/` (pure relocation; `ENGINE_EDITOR_DIR` and `add_subdirectory(editor)` adjusted in root `CMakeLists.txt`; `EngineEditor` target and `<Editor/...>` includes unchanged).
- **Modified code**: `example/physics_example/` (rewritten thin shell, links `PhysicsApp`), root `CMakeLists.txt` (add `app/physics` and `app/editor` subdirectories, adjust `ENGINE_EDITOR_DIR`).
- **Unchanged**: engine modules and `MainClass`; app consumes them through existing public getters only.
- **Known follow-ups (out of scope)**: MSVC export macros, exposing physics parameters from `MainClass`, C API with error codes, install-time relative paths, headless physics loop, external force input and state readback.
- **Verification**: build passes + manual run of the rewritten `physics_example`.
