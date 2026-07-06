## Context

The engine has a GPU convex collision detector that outputs contact points, normals, and penetration depths into GPU buffers. The XPBD solver's `step.comp` compute shader is a placeholder that does not modify rigid body positions. No code path connects collision results to a solver response.

We are implementing a real XPBD (Extended Position Based Dynamics) solver that runs entirely on the GPU via Vulkan compute shaders. The reference implementation is a CUDA-based XPBD multi-body solver that uses Jacobi parallelization. We are adapting this algorithm to GLSL compute shaders using the engine's existing Vulkan render graph infrastructure.

Three architectural problems have been identified in the initial implementation:

1. **Collision detection is external**: `ConvexCollisionDetector` is owned by the example, not the solver — it should be part of the solver's pipeline.
2. **Detection runs once per frame**: The CUDA reference runs collision detection per-substep after force integration. We need to match this to detect contacts at correct simulation positions.
3. **Shape world transforms are stale**: `shape_world_position` and `shape_world_rotation` are uploaded once at scene init and never updated. When rigid bodies move, collision detection reads stale positions. A compute shader must recompute world transforms each substep.

The first phase handles only contact constraints (no joints).

## Goals / Non-Goals

**Goals:**

- Implement full GPU XPBD substep loop: force integration → shape world update → collision detection → Jacobi position solve → velocity update → velocity-level friction solve
- Own the collision detector inside `XPBDGpuSolver` so the external API is simple
- Run collision detection per-substep after force integration (matching CUDA reference)
- Recompute shape world transforms each substep via a dedicated compute shader
- Expose correct buffer dependencies to the render graph for barrier ordering (including shadow maps)
- Manage all intermediate GPU buffers within `XPBDGpuSolver`

**Non-Goals:**

- Joint constraints (hinge, fixed) — deferred to a future change
- Broadphase collision detection — the existing all-pairs GPU approach is sufficient for now
- CPU-side XPBD
- Indirect dispatch — always dispatch MAX workgroups, early-return in shader

## Decisions

### Decision 1: Jacobi parallelization

**Choice**: Jacobi — all constraints solve in parallel, deltas atomically accumulated per-body, averaged before application.

**Rationale**: Gauss-Seidel kills GPU parallelism. Jacobi lets every constraint thread run independently. This is the standard GPU XPBD approach and matches the CUDA reference.

### Decision 2: No intermediate constraint pack step

**Choice**: XPBD shaders directly read collision detection output buffers and the shape→body mapping buffer. Local contact points are computed on-the-fly in the constraint solve shader using the substep-start pose snapshot.

**Rationale**: Saves a kernel dispatch and a constraint buffer allocation vs. the CUDA reference's pack step.

### Decision 3: Multiple small compute shaders

**Choice**: Each phase is a separate `.comp` file and render graph pass. New shaders: `update_shape_world_pose.comp`.

**Rationale**: Matches the existing pattern. Render graph automatically inserts correct barriers between passes based on declared buffer usage. Easier to debug and profile.

### Decision 4: Fixed dispatch size with early-return

**Choice**: All per-constraint dispatches use `(MAX_CONTACTS + 63) / 64` workgroups. Shader threads beyond actual count return immediately.

**Rationale**: Avoids indirect dispatch complexity and CPU readback of contact count.

### Decision 5: Compute shader for buffer snapshots

**Choice**: Dedicated compute shaders that copy buffer A to buffer B (one thread per body).

**Rationale**: Avoids queue family / barrier stage complications from `vkCmdCopyBuffer`. For body counts in the hundreds to low thousands, the overhead is negligible.

### Decision 6: CollisionResultBuffers struct (internal only)

**Choice**: `ConvexCollisionDetector` still exposes `GetCollisionResultBuffers()` returning a POD struct, but this is now called internally by `XPBDGpuSolver::Impl`. The external `Step()` API no longer takes `CollisionResultBuffers`.

**Rationale**: Internal interface stays clean — the solver gets all collision handles from its owned detector. The external caller is simplified to `Step(builder, physics_scene, mm_handle)`.

### Decision 7: Atomic float-add via macro on int SSBO

**Choice**: A GLSL preprocessor macro `ATOMIC_ADD_FLOAT(mem_ref, val)` (in `xpbd_atomic.glsl`) implements portable atomic float addition using `atomicCompSwap` on `int`-typed SSBO members. All delta accumulators and lagrange values are stored as `int[]` in GPU buffers, with `floatBitsToInt`/`intBitsToFloat` used to reinterpret.

**Rationale**: Vulkan GLSL (without `GL_EXT_shader_atomic_float`) does not have native `atomicAdd` for floats. Wrapping `atomicCompSwap` in a macro avoids the `inout` function-parameter l-value issue that `glslangValidator` would reject. Storing floats as ints is the standard portable approach.

### Decision 8: Per-substep collision detection

**Choice**: Collision detection (pair gen + MPR) runs inside the substep loop, immediately after force integration and shape world update.

**Rationale**: The CUDA reference runs collision detection per-substep. Bodies change position during force integration, so contacts must be re-detected at the new positions for correct penetration resolution. The cost is 1–2 extra dispatches per substep, which is negligible compared to correctness gain.

### Decision 9: Shape world pose update shader

**Choice**: A dedicated per-shape compute pass (`update_shape_world_pose.comp`) recomputes `shape_world_position` and `shape_world_rotation` from each shape's owning rigid body pose and local offset.

**Rationale**: The collision detection shader reads `shape_world_position` and `shape_world_rotation` — these must reflect the current simulation state after force integration. The render graph's `UseBuffer` declarations on `rigid_body_center_position` (read) and `shape_world_position` (write) ensure the shape world update runs after force integration and before collision detection.

**Formula**: `world_pos = body_pos + quat_rotate(body_ori, local_pos)`, `world_ori = quat_mul(body_ori, local_ori)`. For unbound shapes (no owning rigid body), world = local.

### Decision 10: Collision detector ownership

**Choice**: `ConvexCollisionDetector` is created and owned by `XPBDGpuSolver::Impl`. Created lazily in a new `EnsureCollisionDetector(shape_count)` method, recreated if shape count changes.

**Rationale**: Collision detection is a logical sub-step of the XPBD pipeline. Internalizing it simplifies the external API and ensures the detector's lifecycle is tied to the solver's. The `CollisionResultBuffers` struct becomes an internal detail. The detector's result buffers are imported by the solver for use in constraint solve passes.

## Data Flow (per substep)

```
for each substep:
  ┌──────────────────────────────────┐
  │ 1. Snapshot pre-gravity pose     │  snapshot_position.comp  (body_count)
  │ 2. Integrate forces              │  integrate_forces.comp   (body_count)
  │ 3. Snapshot pre-contact vel      │  snapshot_position.comp  (body_count)
  │ 4. Snapshot substep-start pose   │  snapshot_position.comp  (body_count)
  │ 5. Update shape world poses      │  update_shape_world_pose.comp (shape_count)  ← NEW
  │ 6. Collision detection           │  generate_pairs + detect_collisions           ← MOVED
  │ 7. Memset lagrange→0             │  clear_int_buffer.comp   (max_contacts)
  │                                  │
  │   for each position iteration:   │
  │ 8. Accumulate contact position   │  accumulate_contact_position.comp (max_contacts)
  │ 9. Apply body position deltas    │  apply_body_position_deltas.comp  (body_count)
  │                                  │
  │ 10. Update velocities from pose  │  update_velocities_from_pose.comp (body_count)
  │                                  │
  │   for each velocity iteration:   │
  │ 11. Accumulate contact velocity  │  accumulate_contact_velocity.comp (max_contacts)
  │ 12. Apply body velocity deltas   │  apply_body_velocity_deltas.comp  (body_count)
  └──────────────────────────────────┘

After all substeps:
  13. Model matrix update            model_matrix.comp (body_count)
```

## GPU Buffers

### Owned by XPBDGpuSolver (unchanged from initial design)

| Buffer | Type | Size | Purpose |
|--------|------|------|---------|
| `pre_gravity_position` | vec4[] | N | Snapshot before force integration |
| `pre_gravity_orientation` | vec4[] | N | Snapshot before force integration |
| `pre_contact_linear_vel` | vec4[] | N | Snapshot before contact solve |
| `pre_contact_angular_vel` | vec4[] | N | Snapshot before contact solve |
| `substep_start_position` | vec4[] | N | Substep-start pose for local→world transforms |
| `substep_start_orientation` | vec4[] | N | Substep-start pose for local→world transforms |
| `linear_position_delta` | int[] | N×3 | Jacobi accumulator (float via int bitcast) |
| `angular_position_delta` | int[] | N×3 | Jacobi accumulator |
| `position_delta_count` | int[] | N | Jacobi accumulator |
| `linear_velocity_delta` | int[] | N×3 | Jacobi accumulator |
| `angular_velocity_delta` | int[] | N×3 | Jacobi accumulator |
| `velocity_delta_count` | int[] | N | Jacobi accumulator |
| `contact_lagrange` | int[] | max_contacts | Per-contact lagrange multiplier |
| `gpu_uniforms` | vec4 | 1 | gravity.xyz + dt (host-visible) |
| `gpu_body_count_buffer` | uint | 1 | Element count for snapshot dispatches |
| `gpu_contact_count_buffer` | uint | 1 | Element count for clear dispatches |

N = max_rigid_bodies, max_contacts = max_collision_pairs × 4

### Owned by ConvexCollisionDetector (unchanged, now internal to solver)

| Buffer | Type | Size |
|--------|------|------|
| `collision_pairs` | uvec2 | max_pairs |
| `collision_ids` | uvec2 | max_pairs × 4 |
| `collision_normals` | vec4 | max_pairs × 4 |
| `contact_point_a` | vec4 | max_pairs × 4 |
| `contact_point_b` | vec4 | max_pairs × 4 |
| `collision_count` | uint | 1 |

### PhysicsScene buffers (written by solver)

| Buffer | Written by |
|--------|-----------|
| `shape_world_position` (vec4) | `update_shape_world_pose.comp` |
| `shape_world_rotation` (vec4) | `update_shape_world_pose.comp` |
| `rigid_body_center_world_position` (vec4) | `integrate_forces.comp`, `apply_body_position_deltas.comp` |
| `rigid_body_center_world_rotation` (vec4) | `integrate_forces.comp`, `apply_body_position_deltas.comp` |
| `rigid_body_linear_velocity` (vec4) | `integrate_forces.comp`, `update_velocities_from_pose.comp`, `apply_body_velocity_deltas.comp` |
| `rigid_body_angular_velocity` (vec4) | `integrate_forces.comp`, `update_velocities_from_pose.comp`, `apply_body_velocity_deltas.comp` |

## Shader Files

```
engine/Physics/shader/solver/XPBDSolver/
├── integrate_forces.comp
├── accumulate_contact_position.comp
├── apply_body_position_deltas.comp
├── update_velocities_from_pose.comp
├── accumulate_contact_velocity.comp
├── apply_body_velocity_deltas.comp
├── snapshot_position.comp
├── clear_int_buffer.comp
├── update_shape_world_pose.comp       ← NEW
├── step.comp                          ← placeholder (no-op)
├── model_matrix.comp
└── common/
    ├── xpbd_math.glsl                 ← MODIFIED: added quat_mul; removed vec3_dot/vec3_cross wrappers (use built-in dot/cross)
    └── xpbd_atomic.glsl               ← ATOMIC_ADD_FLOAT macro for portable float atomic-add on int SSBO
```

## Risks / Trade-offs

- **Jacobi convergence**: Jacobi requires more iterations than Gauss-Seidel. → Mitigation: Configurable `num_iter_persubstep` (default 5) and `num_substep_perstep` (default 1).
- **Substep dispatch overhead**: Adding shape-world-update + collision detection per substep increases total dispatches. For typical scenes (100 shapes, 4 substeps), this adds ~4 dispatches/substep. → Acceptable vs. correctness.
- **Constraint solve uses substep-start local points**: Contact points are computed from the substep-start pose. If bodies rotate significantly during iterations, the contact point drifts slightly. → Mitigation: Few iterations per substep, so drift is minimal. Matches the reference.
- **Duplicate buffer imports**: Both the solver and the detector import the same `ComputeBuffer*`. → The render graph deduplicates by pointer identity and returns the same handle. Verified by existing usage pattern (collision result buffers imported both in solver and detector).
