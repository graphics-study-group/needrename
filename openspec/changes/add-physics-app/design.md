# Design: PhysicsApp DLL

## Context

The engine is a set of module DLLs (Core, Rhi, Asset, Physics, Render, Framework) aggregated behind an `Engine` interface target. An app-layer DLL pattern already exists: `editor/` builds `EngineEditor` and `example/editor_run_game_example` combines `MainClass` + `EngineEditor` in a thin executable.

GPU physics currently exists only as `example/physics_example`: a ~420-line bare `main()` that manually initializes `MainClass`, wires input axes, builds the scene via a local `SceneBuilder`, sets up camera/lights, flushes physics, builds a render graph, and runs `MainClass::MainLoop`. None of this is reusable.

Key codebase facts established during exploration:

- `PhysicsScene`/`XpbdGpuSolver`/`PhysicsSystem` depend only on RHI (`ComputeBuffer`, `DeviceContext`, `SubmissionHelper`) and `vk::CommandBuffer` — the GPU physics chain has no Render-module dependency. The coupling lives in the orchestration layer (`MainClass::RunOneFrame` shares one command buffer between physics and rendering).
- `SubmissionHelper` (RHI) can be constructed independently from `DeviceContext.GetDeviceInterface() + GetAllocatorState()` (as `PhysicsAdaptor` already does) and offers `ExecuteSubmissionImmediately()` (submit + wait).
- Solver auto-registration (hardcoded `XpbdConfig`) happens inside `MainClass::LoadProject`; the app inherits it for free.
- `ComplexRenderGraphBuilder::BuildDefaultRenderGraph` accepts the physics model-matrices buffer directly.
- `WorldSystem::UpdateRendererData` must run per rendered frame (mesh pre-update + light upload).
- Cameras are consumed via `SetActiveCamera` registration of a shared `Camera` object; mutating the camera object after registration is picked up by rendering.

## Goals / Non-Goals

**Goals:**

- Add a `PhysicsApp` DLL as a complete, minimal consumer-facing entry point for GPU physics simulation: build scene → commit → step/render.
- Make `example/physics_example` a thin (~80 line) content-configuration executable.
- Keep engine modules (`Physics`, `Render`, `Framework`, `MainClass`) unchanged.
- Preserve a clean path toward future C API / Python distribution (opaque handles, no World/Asset concepts in consumer code, exceptions at the boundary).

**Non-Goals:**

- Headless physics loop (physics still runs on the app's own loop, not inside `RunOneFrame`, but rendering is still required).
- External force input / state readback (needs GPU synchronization work, deferred).
- C API, install-time relative paths, physical parameter exposure (`XpbdConfig` stays hardcoded in `MainClass`), MSVC export macros, multi-scene rendering.

## Decisions

### 1. App family directory layout, DLL, and naming

All app-layer DLLs live under a unified top-level `app/` family. The existing `editor/` directory is relocated to `app/editor/` (pure move: `EngineEditor` target name, `<Editor/...>` include root, and behavior unchanged; only `ENGINE_EDITOR_DIR` and `add_subdirectory(editor)` in the root `CMakeLists.txt` move). The new `PhysicsApp` lives at `app/physics/`, DLL named `PhysicsApp` (no `Engine` prefix), facade class `PhysicsApp`. Mirrors the editor pattern (`EditorMainClass` is an independent class, not a `MainClass` subclass). No reflection registration for app-internal components (camera controller), no export macros — same approach as `EngineEditor` (MinGW auto-export).

- *Alternatives considered*: keep `editor/` at top level with only physics in `app/` (rejected — two competing layouts for the same app-layer concept); `engine/Physics` submodule (rejected — it is an app, not an engine module); `physics_app/` without an `app/` group (rejected — `app/` groups future app DLLs).

### 2. Non-singleton facade with full initialization in Create

`PhysicsApp` is non-singleton, created via `static std::unique_ptr<PhysicsApp> Create(CreateInfo)` where `CreateInfo` carries only `resol_x`, `resol_y`, `window_title`. `Create` internally performs `MainClass::Initialize`, `LoadBuiltinAssets`, `LoadProject(ENGINE_PROJECTS_DIR/"empty_with_sky")`; paths are read from `cmake_config.h` macros. The app holds a `shared_ptr<MainClass>` to guarantee teardown order. `MainClass::MainLoop` is never used; the app drives its own loop.

- *Alternatives considered*: consumer calls the three MainClass init calls (rejected — future C API/Python consumers have no "main class" concept); app inherits `MainClass` (rejected — singleton pattern and unnecessary coupling).

### 3. Two-phase state machine with freeze via exceptions

One-way `Building → Committed`. All invalid-usage errors throw: `std::logic_error` for wrong-phase calls (build after commit, drive before commit, second `CommitScene`), `std::invalid_argument` for invalid color strings. Freeze covers the physics scene content only (build APIs); runtime controls (`Pause`/`Resume`/`SetCameraPose`/`ShouldQuit`) remain legal. The freeze check lives in the app's build API, not in the World layer, so the internally-managed camera (updated every frame) is not blocked.

- *Alternatives considered*: asserts (rejected — exceptions give a recoverable, future-C-API-mappable boundary); World-layer freeze flag (rejected — would block internal camera updates).

### 4. Split SceneBuilder: physics assembly + visualization assembly

The existing `SceneBuilder` migrates into the app and is split internally: physics assembly (RigidBodyComponent + CollisionShapeComponent + joints, no Asset dependency) and visualization assembly (StaticMeshComponent + builtin meshes + materials). Add methods still produce both parts (external behavior unchanged); the split establishes the boundary so a future headless mode can reuse the physics assembly alone. `GetMeshComponents()` becomes internal; `Finalize()` disappears (its work moves into `CommitScene`).

- *Alternatives considered*: keep SceneBuilder monolithic (rejected — blocks the headless/Python path); bypass the World and build `PhysicsScene` slots directly (rejected — would detach from the component system that joint/physics lifecycle already depends on).

### 5. Opaque BodyId and generic joint API

Add methods return an app-level opaque `BodyId` (index-based; internally maps to `ObjectHandle`). Joints: `AddFixedJoint(obj1, obj2, FixedJointParams{.compliance})` and `AddHingeJoint(obj1, obj2, HingeJointParams{.axis_obj1, .anchor_obj1, .compliance})`, mirroring `PhysicsConstraintComponent` semantics (obj1 owns the component, obj2 is referenced by handle; initial relative transforms derived at Awake from current world poses). No `AddDoublePendulum` — the pendulum becomes example content built from primitives.

- *Alternatives considered*: return `GameObject&` (rejected — leaks World concepts); specific assembly helpers like `AddDoublePendulum` (rejected — app API stays generic).

### 6. Color-string materials

Body descs carry `color` (string). Eight valid names map to `builtin://materials/solid_color_<color>.asset`; empty string picks a random color at Add time; invalid strings throw `std::invalid_argument`. Material `AssetRef`s are resolved at `CommitScene` time. Lights take raw `vec3` color (physical quantity, not an asset reference).

### 7. Step: dedicated command buffer with device-level waitIdle

`Step()` executes: device-level waitIdle → `PreGPUStep()` → record `GPUStep` into a dedicated command buffer → submit on the graphics queue → wait its fence → `PostGPUStep()` → device-level waitIdle. The dedicated command buffer is allocated from `DeviceInterface::GetQueueInfo().graphicsPool` and submitted on `graphicsQueue` directly by the app (the engine's `SubmissionHelper` only exposes upload/clear operations, so the app reuses the same RHI primitives rather than adding an engine API). Device-level (not batch-level) waits are required because the renderer's 3-frame rotation means frame N−1/N−2 GPU work may still be reading `model_matrices` while the next physics step wants to write it; the pre-wait guarantees render reads finished and the post-wait guarantees physics writes finished before the next render frame. Consecutive `Step` calls are allowed (physics fast-forward). `Pause()` wraps `PhysicsScene::SetSimulationEnabled(false)`; the solver no-ops, the app loop is unchanged.

- *Alternatives considered*: batch-level fence only (rejected — does not resolve the model_matrices cross-frame race); multi-buffering model matrices (rejected — complexity for a tool-class app; deferred); extending `SubmissionHelper` with a generic command operation (rejected — would modify the RHI module, violating the zero-engine-change constraint).

### 8. RenderNextFrame: input + camera + one render frame

`RenderNextFrame()`: SDL event poll + input update → camera update (fly controls, migrated `CameraControllerComponent` logic; SPACE toggles pause) → `UpdateRendererData` → `StartFrame` (skip frame on swapchain-out-of-date sentinel) → `BeginMainCommandBuffer` → `RecordAllPasses` → `CompleteFrame` (no wait; next `Step`'s pre-waitIdle absorbs it).

### 9. Camera managed internally, SetCameraPose across phases

The app creates its own camera GameObject/`CameraComponent` (excluded from freeze; updated via the app's internal path). `SetCameraPose(position, look_target)` is optional in Building (default pose looks at world origin) and remains callable after commit.

### 10. CommitScene sequence

`FlushCmdQueue` → `AddInitEvent` + `ProcessEvents` (component Init/Awake, joint slot allocation and initial transforms) → `FlushPhysics` (adaptor conversion + `SyncGpuBuffers`) → `SetModelMatricesBuffer` (physics→render bridge) → `BuildDefaultRenderGraph` (via `ComplexRenderGraphBuilder`) → set freeze flag. Simulation starts paused (matches the current example's start experience).

### 11. Example stays content-only

`example/physics_example` links `PhysicsApp` (replacing the direct `Engine` link), keeps both `AddTemplateScene` and `AddTemplateScene2` content functions (scene 2 commented out), rebuilds the double pendulum via the joint API, sets initial camera pose and lights, and runs the three-line loop. The migrated camera controller and removed `SimulationToggleComponent` disappear from the example.

## Risks / Trade-offs

- **Per-step device waitIdle kills CPU/GPU overlap** → accepted: tool-class app; future external input/readback requires waiting anyway; deferred performance work (fences, multi-buffering) is noted.
- **App loop duplicates physics orchestration already in `RunOneFrame`** → accepted: `RunOneFrame` stays for game/editor paths; duplication is small and contained in the app; future extraction into a shared helper is possible.
- **Hardcoded `XpbdConfig`/paths in `MainClass`/`cmake_config.h` become app limitations** → accepted for this iteration; listed as follow-ups (parameter exposure, install-relative paths).
- **Exceptions across DLL boundary** → MinGW unified runtime makes this safe today; future C API must convert exceptions to error codes at the boundary (documented as a follow-up).
- **Freeze enforcement is app-layer only** → a consumer with engine access could mutate the World directly; accepted (consumers are expected to use the app API only; the World layer is not modified).
- **`MainClass` remains a singleton** → multiple simultaneous `PhysicsApp` instances share one MainClass; documented limitation, out of scope.

## Migration Plan

1. Relocate `editor/` → `app/editor/` and adjust `ENGINE_EDITOR_DIR` / `add_subdirectory(editor)` in the root `CMakeLists.txt`; verify `editor_run_game_example` still builds and links.
2. Add `app/physics/` (CMake + sources) and register the subdirectory in the root `CMakeLists.txt`.
2. Migrate and split `SceneBuilder` into the app; migrate `CameraControllerComponent`; delete `SimulationToggleComponent`.
3. Rewrite `example/physics_example` as the thin shell and relink it against `PhysicsApp`.
4. Build (`cmake --preset debug`) and manually verify: freeze exceptions, pause/resume via SPACE, camera controls, joint-correct double pendulum, window close quit, step fast-forward.
5. No engine-module changes; no data migration.

## Open Questions

- Install-time relative path strategy for assets/projects (deferred until the first API distribution).
- Whether `XpbdConfig` exposure belongs to the app or to `MainClass` refactoring (deferred).
- C API shape and error-code mapping (deferred).
- Headless physics loop extraction (deferred).
