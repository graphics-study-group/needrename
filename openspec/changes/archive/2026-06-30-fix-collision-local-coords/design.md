## Context

The XPBD solver pipeline within each substep is:

```
PreCollisionRG: Snapshot(p, q) → IntegrateForces(p, q, v, w) → UpdateShapeWorldPose
BroadPhase:     Detect candidate pairs via spatial hash
NarrowPhase:    MPR detection → contact points in world space
PostCollision:  Clear Lagrange multipliers
PositionIter:   Per-contact accumulate → Per-body apply (repeat N times)
PostPosition:   Update velocities from pose delta (v = (p_new - p_start) / dt)
VelocityIter:   Per-contact accumulate friction+restitution → Per-body apply (repeat M times)
```

The bug: `Snapshot(p, q)` saves `substep_start` **before** `IntegrateForces`. Collision detection runs on the **integrated** positions. But the constraint solvers use `substep_start` to convert world-space contact points to local coordinates:

```glsl
// accumulate_contact_position.comp (current — buggy)
vec3 r_a_local = quat_inv_rotate(ori_a_start, world_pt_a - pos_a_start);  // ❌ pos_a_start is pre-integration!
vec3 r_a = quat_rotate(ori_a, r_a_local);                                  // Current ori, stale local r
```

The `pos_a_start` is from before force integration, but `world_pt_a` is at the integrated position. This offset error produces incorrect lever arms `r_a`, `r_b` for both position and velocity constraint solving.

### Constraints

- All computation is GPU-side via Vulkan compute shaders (GLSL 450)
- Render graph construction happens at record time (no GPU execution during build)
- `substep_start` must remain for `update_velocities_from_pose.comp`
- Shape local offset (`shape_local_position`, `shape_local_rotation`) is constant and already available in GPU scene buffers

### Known bugs

**Bug 1 — Stale substep_start for contact coordinate conversion**: `Snapshot(p, q)` saves `substep_start` **before** `IntegrateForces`. Collision detection runs on the **integrated** positions. But the constraint solvers use `substep_start` to convert world-space contact points to local coordinates:

```glsl
// accumulate_contact_position.comp (current — buggy)
vec3 r_a_local = quat_inv_rotate(ori_a_start, world_pt_a - pos_a_start);  // ❌ pos_a_start is pre-integration!
vec3 r_a = quat_rotate(ori_a, r_a_local);                                  // Current ori, stale local r
```

The `pos_a_start` is from before force integration, but `world_pt_a` is at the integrated position. This offset error produces incorrect lever arms `r_a`, `r_b` for both position and velocity constraint solving.

**Bug 2 — Pre-contact velocity snapshotted before gravity**: The velocity solver in `accumulate_contact_velocity.comp` uses `PreContactLinearVelocity`/`PreContactAngularVelocity` for restitution reference:

```glsl
vec3 pre_contact_vel_a = pre_v_a + cross(pre_w_a, r_a);
float vn_prev = dot(n, pre_rel_vel);  // ❌ missing gravity contribution!
float normal_delta_v = -vn + min(-restitution * vn_prev, 0.0);
```

These velocity snapshots are taken BEFORE force integration, so `vn_prev` lacks the gravity impulse added during `integrate_forces`. Every substep, gravity adds velocity to bodies, but the restitution reference doesn't reflect it. For resting contacts, this causes a spurious residual bounce because the correction `-vn + min(-restitution * vn_prev, 0.0)` uses a mismatched `vn` (with gravity) and `vn_prev` (without gravity).

## Goals / Non-Goals

**Goals:**
- Fix the coordinate frame mismatch between collision detection and constraint solving (Bug 1)
- Fix the pre-contact velocity snapshot timing for correct restitution/friction (Bug 2)
- Eliminate `substep_start` dependency from contact constraint solvers
- Keep contact normals in world space (already correct)
- Minimize shader binding changes

**Non-Goals:**
- Changing the MPR or perturbation algorithms
- Changing the velocity update from pose delta (still uses substep_start correctly)
- Changing joint constraint solvers (they work in body-local space already)
- Adding a separate "convert contact coords" compute pass (inline in existing shaders)

## Decisions

### Decision 1: Shape-local coordinates (not body-local)

**Chosen**: Collision detection outputs contact points in **shape-local** space.

**Rationale**: The collision detection shader works with shapes directly. Each shape has a world pose computed from `body_pose * shape_local_offset`. Converting a world-space contact point to shape-local requires only the shape's world pose (already available in the collision shader):

```glsl
local_pt = quat_inv_rotate(shape_world_rot, world_pt - shape_world_pos)
```

If we used body-local coordinates, we'd need the body→shape inverse transform, adding complexity. Shape-local is the natural frame of the collision detection already.

**Alternatives considered**:
- *Body-local*: Requires `local_pt = quat_inv_rotate(body_rot, world_pt - body_pos)`, but this loses shape offset information. The solver would need to account for the shape offset separately. Rejected as unnecessarily complex.
- *Keep world-space + fix snapshot timing* (move snapshot after integration): Simpler code change, but would require `update_velocities_from_pose` to handle the different snapshot semantics (velocity from force was already applied, so `(pos_new - pos_after_integration) / dt` only captures correction). This would break velocity computation. Rejected.

### Decision 2: Convert in detection shader, not in a separate post-pass

**Chosen**: Convert world-space contact points to shape-local at the end of the collision detection shader (`detect_collisions.comp`).

**Rationale**: The MPR and perturbation algorithms naturally produce world-space points (support queries work in world space). Converting at the end of `main()`, just before writing to output buffers, is a minimal change — 2 lines per contact point. A separate post-pass would require an additional compute dispatch and introduce an extra RG.

**Alternatives considered**:
- *Separate post-pass*: Could batch-convert after detection. Rejected — adds dispatch overhead for a trivial arithmetic operation.
- *Make MPR/perturbation work in local space*: Would require transforming support queries, the CSO, and the entire algorithm. Invasive and error-prone. Rejected.

### Decision 3: Solver reconstructs world contact points from shape-local + current body pose

**Chosen**: In `accumulate_contact_position.comp` and `accumulate_contact_velocity.comp`:
1. Read shape-local contact point from buffer
2. Read shape local offset (`ShapeLocalPosition`, `ShapeLocalRotation`)
3. Read current body pose (`RigidBodyCenterPosition`, `RigidBodyCenterRotation`)
4. Compute world contact point:
   ```glsl
   vec3 local_pt_in_body = quat_rotate(shape_local_rot, local_contact_pt) + shape_local_pos;
   vec3 world_pt = body_pos + quat_rotate(body_rot, local_pt_in_body);
   ```
5. Compute `r = world_pt - body_pos` (lever arm from body center)

**Rationale**: This uses the **current** body pose (which evolves across position iterations), eliminating the stale `substep_start` entirely from contact solving. The shape local offset is constant and correct regardless of body motion.

### Decision 4: Contact normals remain in world space

**Chosen**: The `collision_normals` buffer continues to store world-space normals. No conversion needed.

**Rationale**: The contact normal is a direction, not a position. It is already correctly computed by MPR in world space and doesn't suffer from the coordinate frame mismatch. The solver uses it directly without needing `substep_start`. Converting it to local would add unnecessary complexity.

### Decision 5: Shape local position/rotation buffers added to solver bindings

**Chosen**: Add `ShapeLocalPosition` and `ShapeLocalRotation` as read-only buffer bindings in `accumulate_contact_position.comp` and `accumulate_contact_velocity.comp`. These replace `SubstepStartPosition` and `SubstepStartOrientation` bindings (removed from contact solvers only).

**Rationale**: These buffers already exist in the PhysicsScene GPU state and are read by `update_shape_world_pose.comp`. The contact solvers need them to compose the body→shape→world transformation. The binding slots freed by removing substep_start are reused.

### Decision 6: substep_start snapshots retained for velocity update only

**Chosen**: `BuildPreCollisionRG` still snapshots `SubstepStartPosition` and `SubstepStartOrientation` at the start of each substep (before force integration). These are only consumed by `update_velocities_from_pose.comp` in `BuildPostPositionRG`, which correctly uses them for `v = (p_new - p_start) / dt`.

**Rationale**: The velocity update captures the **total** displacement during the substep (force integration + position correction). The snapshot timing (before integration) is correct for this purpose. Only the contact solvers' usage was wrong.

### Decision 7: Pre-contact velocity snapshots moved to after force integration

**Chosen**: In `BuildPreCollisionRG`, the `PreContactLinearVelocity` and `PreContactAngularVelocity` snapshot passes are moved to after `Integrate Forces` but before `Update Shape World Pose`. The new pass order is:

```
Pass 1: Snapshot SubstepStartPos       (reads pos_h,  before integration — for velocity-from-pose)
Pass 2: Snapshot SubstepStartOri       (reads rot_h,  before integration — for velocity-from-pose)
Pass 3: Integrate Forces               (reads+writes pos/rot/linvel/angvel — gravity applied here)
Pass 4: Snapshot PreContactLinVel      (reads linvel_h, AFTER integration — for restitution reference)
Pass 5: Snapshot PreContactAngVel      (reads angvel_h, AFTER integration — for restitution reference)
Pass 6: Update Shape World Pose        (reads pos/rot — final pose for collision detection)
```

**Rationale**: The pre-contact velocity is used by the velocity solver as the reference for restitution (`vn_prev`). It must reflect the velocity at the moment collision detection runs — which is after gravity/forces have been applied. If snapshotted before integration, gravity's contribution each substep creates a mismatch between `vn` (current, with gravity) and `vn_prev` (reference, without gravity), causing spurious bounce corrections on resting contacts.

**Synchronization analysis**:

| Buffer | PreCollisionRG Import | Integrate Pass | Snapshot Pass (moved) | Final State → Next RG |
|--------|----------------------|----------------|----------------------|----------------------|
| `linvel_h` | `Impl::RW` | `UseBuffer(RW)` | `UseBuffer(RR)` | Written then read — RG builder inserts barrier |
| `angvel_h` | `Impl::RW` | `UseBuffer(RW)` | `UseBuffer(RR)` | Written then read — RG builder inserts barrier |
| `precont_lv_h` | `Impl::RW` | — (unused) | `UseBuffer(WW)` | Written — VelocityIterRG imports as `Impl::RR` |
| `precont_av_h` | `Impl::RW` | — (unused) | `UseBuffer(WW)` | Written — VelocityIterRG imports as `Impl::RR` |

**No `UseBuffer` changes needed**: The snapshot passes already declare `UseBuffer(src_h, Impl::RR)` for the source and `UseBuffer(dst_h, Impl::WW)` for the destination. Moving them after the integrate pass doesn't change their access pattern.

**No `prev_access` changes needed**: The existing convention is:
- Writer (PreCollisionRG): imports internal buffers with conservative `Impl::RW`, writes via `UseBuffer(WW)`. The RG builder tracks final state as "written."
- Reader (VelocityIterRG): imports with `Impl::RR` = `{AT::ShaderRandomRead}`. The render graph system inserts the write→read barrier across independently-built RGs.

This is the same pattern already used for `ssp_pos_h`/`ssp_ori_h` (write in PreCollisionRG → read in VelocityIterRG).

**Within-RG barriers**: The RG builder processes passes in declaration order. It tracks the state of each buffer and inserts pipeline barriers when access patterns change (e.g., RW→RR for `linvel_h` between Integrate and Snapshot). No manual barrier code needed.

**Cross-RG validation**: The intermediate RGs between PreCollisionRG and VelocityIterRG (both detectors, PostCollisionPreIterRG, PositionIterRG, PostPositionRG) do not import `precont_lv_h` or `precont_av_h`. Only VelocityIterRG reads them. The write→read transition is a clean handoff with no intervening accesses.

## Risks / Trade-offs

- **[Risk] Shape-local contact points are only valid while the shape's local offset is constant** → Mitigation: Shape local offsets are indeed constant (set at scene creation). If dynamic shape attachment is added in the future, contact points would need re-validation.

- **[Risk] Extra buffer reads in contact solvers** → Mitigation: Each contact solver invocation already reads 10+ buffers; adding 2 more (shape local pos/rot) adds negligible memory bandwidth overhead. The shape local buffers are small (one vec4 per shape) and cache-friendly.

- **[Trade-off] Slightly more arithmetic in solvers** → The world-space reconstruction requires an extra quaternion rotation and vector addition per body per contact. This replaces the existing `quat_inv_rotate` (world→local) + `quat_rotate` (local→world) pair in the old code. The arithmetic cost is equivalent — one additional `quat_rotate` for the shape-to-body offset, but one fewer `quat_inv_rotate`. Net change: ~zero.

- **[Risk] Collision detection output format changes could affect debug tooling** → If any debug visualization reads `contact_point_a`/`contact_point_b` expecting world-space coordinates, it will display incorrectly. The `physics_example` currently reads these for debug output. Mitigation: Update debug output in the example to convert back to world space for display.
