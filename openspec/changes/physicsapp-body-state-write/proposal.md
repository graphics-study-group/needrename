# Proposal: PhysicsApp per-body state writes

## Why

`PhysicsApp` can read back body state (`GetBodyState` / `GetBodyStates`) but cannot write it. External control loops — test harnesses today, RL / Python driving later — need to override per-body position, rotation, velocities and inject external forces/torques before each `Step`. The engine's own physics path is deliberately excluded: it submits physics on the rotating multi-frame render command buffers without a `waitIdle` discipline, so runtime CPU writes there would need per-frame-in-flight buffering that no consumer needs yet.

## What Changes

- New Drive-phase API on `PhysicsApp`: `SetBodyValue(BodyId id, BodyField field, glm::vec4 value)` covering exactly six fields: `Position`, `Rotation`, `LinearVelocity`, `AngularVelocity`, `ExternalForce`, `ExternalTorque`. Rotation stores a quaternion (xyzw); all other fields store xyz with w=0.
- Direct-overwrite semantics: a set value directly replaces the GPU buffer slot on the next `Step` and persists until overwritten again. External force/torque are NOT cleared by the solver — their lifetime is managed by the caller, matching `RigidBodyComponent`'s constant-configuration semantics. Engine behavior is untouched.
- Upload path mirrors the existing readback design: persistent per-field staging buffers (`StagingToDevice`) allocated at `CommitScene`, whole-field dirty flags, `copyBuffer` uploads recorded in `Step`'s dedicated command buffer before `GPUStep`, dirty flags cleared after the fence wait.
- Pause model rework (**BREAKING** for app-level semantics): `Step()` runs unconditionally; `Pause()` / `Resume()` only flip an app-level flag; the caller checks `IsPaused()` to decide whether to call `Step`. `CommitScene` enables scene simulation once and never toggles it again. The built-in SPACE toggle in `RenderNextFrame` keeps controlling pause/resume through the flag.
- Paused-loop robustness: because the paused caller loop never calls `Step` (whose device waits previously throttled the renderer and produced model matrices), `CommitScene` now seeds the initial model matrices via a one-time no-advance solver pass (bodies stay visible while paused), and `RenderNextFrame` drains the device at the end of each paused frame (no swapchain semaphore-reuse validation errors).
- Engine layer: no changes. No solver clear pass, no `PhysicsScene` API changes, no shader changes.

## Capabilities

### New Capabilities

- `physics-app-body-write`: Drive-phase per-body state write API on `PhysicsApp` and its pre-step GPU upload semantics.
- `physics-app-pause`: app-level pause flag model that replaces the scene simulation-enabled driven pause.

### Modified Capabilities

<!-- No existing capability requirements change; the engine layer is untouched. -->

## Impact

- `app/physics/PhysicsApp.h` / `PhysicsApp.cpp`: new enum, API, upload staging, dirty tracking, upload recording in `Step`, pause rework, `CommitScene` changes (simulation enable-once, model-matrix seed pass), `RenderNextFrame` changes (paused frame drain).
- `test/physics_app_windowed_test.cpp`: main loop checks `IsPaused()` before calling `Step`.
- `test/physics_app_physics_only_test.cpp` / `physics_app_offscreen_test.cpp`: new scenarios for `SetBodyValue` (teleport, velocity injection, force persistence and caller-managed zeroing).
- Engine (`engine/Physics`, `engine/Framework`, `engine/Rhi`): none.
- Explicitly deferred: batch write API (planned for future GPU-array exposure to Python) and editor-path runtime writes (needs per-frame-in-flight staging design). `BodyId` stays an app-registry index — the observed `BodyId == slot` coincidence is not guaranteed and is not relied upon.
