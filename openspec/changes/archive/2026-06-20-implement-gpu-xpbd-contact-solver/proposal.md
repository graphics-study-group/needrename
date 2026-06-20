## Why

The XPBD solver compute shader (`step.comp`) is currently a placeholder that does not modify rigid body positions. The collision detection pipeline produces contact points on the GPU, but (a) detection runs only once per frame rather than per-substep, (b) shape world transforms are stale because they are never recomputed after rigid bodies move, and (c) the collision detector is owned externally rather than being part of the solver pipeline. This change implements a complete GPU XPBD contact solver with per-substep collision detection and shape world pose updates.

## What Changes

- **New compute shader**: `update_shape_world_pose.comp` — recomputes each shape's world-space position and rotation from its owning rigid body's current pose and the shape's local offset, each substep
- **New GLSL helper**: `quat_mul` in `xpbd_math.glsl` for quaternion multiplication
- **ConvexCollisionDetector owned by XPBDGpuSolver** — internalized as part of `Impl`, created lazily from `shape_slot_count`. External callers no longer need to create or manage a collision detector.
- **Per-substep collision detection** — collision detection (pair gen + MPR) now runs inside the substep loop after force integration and shape world update, so contacts are detected at the current simulation positions matching the CUDA reference architecture
- **`XPBDGpuSolver::Step()` signature simplified** — no longer takes `CollisionResultBuffers`; only needs `RenderGraphBuilder`, `PhysicsScene`, and optional model-matrix handle
- **Shadow map passes declare model matrix dependency** in `PhysicsExampleRenderGraphBuilder` to ensure correct render graph barrier ordering
- **Previous shaders retained**: `integrate_forces.comp`, `accumulate_contact_position.comp`, `apply_body_position_deltas.comp`, `update_velocities_from_pose.comp`, `accumulate_contact_velocity.comp`, `apply_body_velocity_deltas.comp`, `snapshot_position.comp`, `clear_int_buffer.comp`, plus shared GLSL headers

## Capabilities

### New Capabilities

- `xpbd-contact-solve`: GPU XPBD contact constraint solving — Jacobi parallelization, lagrange multiplier accumulation, position-level penetration resolution, velocity-level friction and restitution, per-substep collision detection, shape world pose update

### Modified Capabilities

- `physics-gpu-shaders`: XPBDGpuSolver now loads and dispatches multiple solver shaders including the new `update_shape_world_pose.comp`. Step() signature changed — no longer requires externally-provided `CollisionResultBuffers`. Collision detector is internalized.

## Impact

- **`engine/Physics/shader/solver/XPBDSolver/`** — new `update_shape_world_pose.comp`; new `quat_mul` in `common/xpbd_math.glsl`
- **`engine/Physics/Solver/XPBDGpuSolver.h`** — `Step()` signature simplified (removes `CollisionResultBuffers` parameter); `XpbdConfig` gravity default is Z-down
- **`engine/Physics/Solver/XPBDGpuSolver.cpp`** — Impl owns `ConvexCollisionDetector`; substep loop restructured with per-substep shape-world-update + collision detection
- **`example/physics_example/PhysicsExampleRenderGraphBuilder.h/.cpp`** — `m_collision_detector` removed; `Step()` call simplified; shadow map passes declare model matrix dependency
- **`engine/Physics/PhysicsScene.h/.cpp`** — `rigid_body_inverse_inertia` buffer added to `PhysicsGpuBuffers` for XPBD constraint solve
