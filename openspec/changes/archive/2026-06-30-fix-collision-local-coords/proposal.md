## Why

Two related bugs in the XPBD solver cause incorrect constraint solving:

1. **Stale substep_start for contact coordinate conversion**: The solver converts world-space contact points to local coordinates using `substep_start` positions/orientations — but `substep_start` is snapshotted **before** force integration, while collision detection runs **after** integration. The solver therefore computes local contact offsets (`r_a_local`, `r_b_local`) in a coordinate frame that does not match the state collision detection actually used, producing incorrect lever arms for both position and velocity constraint solves.

2. **Pre-contact velocity snapshotted before gravity**: `PreContactLinearVelocity` and `PreContactAngularVelocity` are snapshotted **before** force integration, so they lack the gravity impulse applied during `integrate_forces`. The velocity solver uses these for restitution reference velocity computation. Since gravity adds velocity every substep, the missing contribution causes spurious restitution bounces even for resting contacts — objects sitting on the floor appear "jittery" because `vn_prev` (pre-contact normal velocity) is always slightly off.

## What Changes

- **Collision detection outputs shape-local contact points instead of world-space**. In `detect_collisions.comp`, after computing world-space contact points, convert them to shape-local space using the shape's world pose at detection time. The `contact_point_a` and `contact_point_b` buffers change semantics from world-space to shape-local coordinates.
- **Constraint solvers convert shape-local to world using current body pose**. In `accumulate_contact_position.comp` and `accumulate_contact_velocity.comp`, the `SubstepStartPosition` and `SubstepStartOrientation` buffer bindings are removed. Instead, shape-local contact points are transformed to world space using the shape's local offset and the body's current pose, computing `r_a`, `r_b` directly.
- **Shape local offset buffers added to constraint solver bindings**. The position and velocity constraint shaders gain read-only access to `ShapeLocalPosition` and `ShapeLocalRotation` buffers (previously only available in the collision and shape-update passes).
- **`SubstepStartPosition`/`SubstepStartOrientation` removed from contact solvers** but retained for `update_velocities_from_pose.comp` (which correctly uses them for velocity-from-pose-delta computation).
- **Pre-contact velocity snapshots moved to after force integration**. In `BuildPreCollisionRG`, the `PreContactLinearVelocity` and `PreContactAngularVelocity` snapshot passes are reordered to run after `Integrate Forces` but before `Update Shape World Pose`. This ensures the restitution reference velocity includes the gravity impulse applied during integration, producing stable resting contacts and correct bounce behavior. No `UseBuffer` or `prev_access` changes are required — the RG builder handles barriers between reordered passes automatically.

## Capabilities

### New Capabilities

None — this is a bug fix scoped to existing capabilities.

### Modified Capabilities

- `gpu-convex-collision-detection`: **BREAKING** — `ContactPointA` and `ContactPointB` output buffers change from world-space `vec4` to shape-local `vec4`. Collision detection shader now converts world-space contact points to shape-local before writing.
- `xpbd-contact-solve`: **BREAKING** — `accumulate_contact_position.comp` and `accumulate_contact_velocity.comp` remove `SubstepStartPosition`/`SubstepStartOrientation` bindings and replace with `ShapeLocalPosition`/`ShapeLocalRotation`. Local-to-world conversion uses current body pose instead of stale substep-start pose. **Additionally**: `PreContactLinearVelocity` and `PreContactAngularVelocity` now reflect post-integration velocity (including gravity), fixing restitution reference computation.
- `xpbd-solver-multi-rg`: `BuildPositionIterRG` and `BuildVelocityIterRG` import `ShapeLocalPosition`/`ShapeLocalRotation` buffers instead of `SubstepStartPosition`/`SubstepStartOrientation` for contact passes. `BuildPreCollisionRG` reorders snapshot passes: substep-start position/orientation snapshots remain first (for velocity-from-pose), then integrate forces, then pre-contact velocity snapshots (for restitution reference), then update shape world poses.

## Impact

- **Modified shaders**: `detect_collisions.comp`, `accumulate_contact_position.comp`, `accumulate_contact_velocity.comp`
- **Modified C++**: `XPBDGpuSolver.cpp` (RG build functions: reorder snapshots in `BuildPreCollisionRG`, remove substep_start bindings from contact passes in `BuildPositionIterRG`/`BuildVelocityIterRG`, add shape local buffers)
- **No UseBuffer / prev_access changes**: Internal RG pass reordering is handled by the RG builder automatically; cross-RG prev_access contracts (PreCollisionRG writes, VelocityIterRG reads) remain unchanged
- **Unchanged**: `integrate_forces.comp`, `update_velocities_from_pose.comp`, `snapshot_position.comp`, all joint constraint shaders, all broad-phase shaders
- **No API changes**: `XpbdGpuSolver.h` unchanged, `ConvexCollisionDetector.h` unchanged (buffer semantics change but not the API surface)
- **No new files**, no build system changes
