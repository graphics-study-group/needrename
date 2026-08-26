## Why

`PhysicsApp` currently runs in a single windowed form, which cannot serve headless environments (CI, RL training loops, automated tests) and cannot expose simulation results to CPU consumers. The engine's headless presenter is a stub that discards every rendered frame and hard-codes a 1920×1080 extent, so there is no offscreen render path worth building on.

## What Changes

- **BREAKING** `AppMode` execution modes on `PhysicsApp`: `PhysicsOnly` (physics only, no rendering/window/camera), `Offscreen` (physics + render to an internal CPU-readable texture, no window), `Windowed` (current state). Mode is set in `CreateInfo.mode` and immutable after create.
- **Physics readback**: `BodyId` resolves to rigid-body slot indices via a mapping built at `CommitScene`; `GetBodyState(id)` and `GetBodyStates()` expose position / rotation / linear & angular velocity + per-body center-of-mass offsets, valid even before the first `Step`.
- **Render readback**: opt-in toggle `SetRenderReadbackEnabled(bool)` (default off) + blocking `GetRenderOutput()` returning the latest frame's pixels (RGBA8), with a documented skip-frame and data-validity contract.
- **Engine general capabilities**: `CommandBuffer::RecordCopyImageToBuffer` copy recorder (plus a local copy in `OffscreenPresentProvider`, which records without an `Engine::CommandBuffer`), and `FrameManager::WaitForFrameCompletion()` for precise frame-completion waits.
- **BREAKING** `HeadlessPresentProvider` is replaced by `OffscreenPresentProvider`: real per-frame-in-flight host-visible present targets, lazily allocated, and the hard-coded headless extent is replaced by a unified `RenderSystem` ctor `extent` parameter (default `{0,0}`): in windowed mode a zero extent is derived via `SDL_GetWindowSizeInPixels`; in headless mode a zero extent throws (no window to derive from), so the extent is fed from `StartupOptions::resol_x/resol_y`. Readback-callback API on the provider is deferred.
- **Mode 1 simplification**: camera, lights, and render graph are skipped entirely (`SceneBuilder` gains a `with_visuals` switch); render-only APIs throw `std::logic_error` in mode 1.
- **BREAKING** `example/physics_example` is removed; its scene builders move into a new windowed test executable.
- Three test executables (`physics_app_physics_only_test`, `physics_app_offscreen_test`, `physics_app_windowed_test`) replace the example; the windowed test runs finite frames+auto-resume under ctest and infinite frames+paused under manual launch.

## Capabilities

### New Capabilities
- `physics-app-modes`: `PhysicsApp` execution modes (physics-only / offscreen / windowed), the `BodyId → rigid-body index` mapping, physics state readback API, and opt-in render readback API.

### Modified Capabilities
- `headless-present-pipeline`: replace `HeadlessPresentProvider` with `OffscreenPresentProvider` (real offscreen present targets + copy, lazy allocation), and flow the render extent from `StartupOptions` instead of a hard-coded 1920×1080.

## Impact

- `app/physics/` — `PhysicsApp.h/.cpp`, `SceneBuilder.h/.cpp`: mode plumbing, readback buffers/APIs, mode-1 simplification.
- `engine/Render/RenderSystem.cpp|.h`, `RenderSystem/FrameManager.h/.cpp`, `RenderSystem/HeadlessPresentProvider.h/.cpp` (+ new `OffscreenPresentProvider`), `Render/Pipeline/CommandBuffer.h/.cpp` (`RecordCopyImageToBuffer` member): engine capabilities.
- `engine/Framework/MainClass.cpp`: forward headless extent (non-zero) into `RenderSystem`; windowed mode passes `{0,0}` (derived from the window).
- `example/physics_example/` — removed; scene content relocated to `test/physics_app_windowed_test.cpp`.
- `test/` — three new executables linking `Engine` + `PhysicsApp`.
- No changes to Physics engine internals (solver/scene), FrameManager readback-callback mechanism, or PhysicsSystem.
