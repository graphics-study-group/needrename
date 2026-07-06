## Context

The GPU XPBD solver runs on Vulkan compute shaders and integrates into the render graph via `XPBDGpuSolver`. PhysicsScene manages CPU-side SoA (Structure of Arrays) vectors for rigid bodies and shapes, mirrored to GPU through `PhysicsGpuBuffers`. Components register with PhysicsScene during the `Awake()` lifecycle hook, where they resolve handle→index mappings.

A reference CUDA implementation (`reference_code/`) provides working GPU kernels for hinge and fixed constraints that can be ported to GLSL. The design follows the same XPBD Jacobi formulation reviewed during exploration.

## Goals / Non-Goals

**Goals:**
- Support arbitrary numbers of hinge and fixed joints per scene
- Joint constraints solved in parallel with other constraints (contacts), sharing the same delta/count buffers
- Editable component properties for local attach points and axes
- Object handle resolution, logging invalid joints as errors without crashing the simulation
- Visual validation via a double-pendulum scene

**Non-Goals:**
- Hinge angle limits (removed from this change — can be added later if needed)
- Hinge motor / target angle (removed)
- Velocity-level solving for joints (position-level only for now)
- Joints between more than two bodies (each joint is binary)
- Joint-aware collision filtering (standard collision detection runs as-is)

## Decisions

### D1: AoS for joint input data, SoA for Lagrange multiplier runtime state

**Choice:** Joint definition data (indices, axes, attach points, compliance, initial relative transform) is stored as packed AoS buffers owned by PhysicsScene. Lagrange multiplier runtime state (`position_lagrange`, `rotation_lagrange`, `aligned_axis_lagrange`) is stored as separate SoA float buffers owned by `XPBDGpuSolver::Impl`.

**Rationale:** Joint definitions are static after initialization — they never change during simulation. Keeping them as packed AoS gives good cache locality when each thread reads one complete joint. Lagrange multipliers, however, are read and written every iteration by the accumulate kernels. Storing them as separate SoA buffers means the clear kernels can zero them in one pass (one `float` per constraint, no struct parsing), and the accumulate kernels read/write only the needed multiplier fields. This also avoids write conflicts where the accumulate kernel writes back the entire struct (including static fields) just to update one float.

**Alternatives considered:** Embedding lagrange in the AoS struct (as in the CUDA reference). Rejected because: (a) the clear pass would need to know the struct layout to zero only lagrange fields, (b) the accumulate kernels would need to read-modify-write entire structs, and (c) it couples static data lifetime to runtime state lifetime.

### D2: Initial relative transform snapshot for FixedJoint

**Choice:** FixedJoint snapshots `q1_initial⁻¹ * q2_initial` and `q1_initial⁻¹ * (pos2_initial - pos1_initial)` (relative position in obj1's local frame) at `Awake()` time. The solver drives them back toward the initial snapshot.

**Rationale:** FixedJoint should act as a rigid weld — lock object 2 to object 1's local frame. Snapshotting the initial transform means the joint naturally adapts to whatever placement exists at creation time. No user-specified attach points or relative rotation needed. This matches typical physics engine semantics for a "fix" joint at creation time.

**Alternatives considered:** Driving relative rotation to zero (fully aligned orientations, as the CUDA reference does). Rejected because snapshotting the initial relative pose is more flexible than "ball-socket about COM + full orientation lock".

### D3: Hinge constraint formulation (no angle limits)

**Choice:** Hinge solves two independent constraints: (a) axis alignment — `cross(axis1_world, axis2_world) → 0`, and (b) attachment point coincidence — `attach_point2_world - attach_point1_world → 0`. Both use XPBD compliance.

**Rationale:** This is the standard hinge formulation from the CUDA reference (`accumulateHingeJointPositionDeltasKernel`), minus the angle limit portion. The aligned-axis constraint ensures the hinge axes of both bodies stay parallel. The attachment point constraint ensures they remain connected at the hinge point. This provides all the degrees of freedom a hinge needs (rotation about the aligned axis) for the double-pendulum demo.

### D4: Jacobi shader pattern (all constraints share delta buffers)

**Choice:** Hinge and fixed joint shaders atomically add to the same `LinearPositionDeltaI`, `AngularPositionDeltaI`, and `PositionDeltaCount` buffers used by the contact constraint solver. A single apply pass averages the deltas once per iteration.

**Rationale:** This is the XPBD Jacobi parallelization pattern — each constraint independently contributes correction deltas, then each body averages all contributions. Optimal for parallelism (1 thread per constraint, no lock contention). Matches the reference CUDA implementation and the existing contact solver. No special handling needed for bodies participating in multiple constraint types (contact + joints) — all contributions are summed and averaged.

**Alternatives considered:** Separate delta buffers per constraint type, solved sequentially. Rejected because it breaks the Jacobi method's advantage (contributions from different constraints converge together) and adds unnecessary complexity.

### D5: Shader loading and instantiation

**Choice:** Follow the existing pattern: SPIR-V loaded from `ENGINE_PHYSICS_SPIRV_DIR/solver/XPBDSolver/`. New shaders (`accumulate_hinge_position.comp`, `accumulate_fixed_position.comp`, `clear_hinge_lagrange.comp`, `clear_fixed_lagrange.comp`) are managed as `ComputeStage` instances owned by `XPBDGpuSolver::Impl`.

### D6: PhysicsConstraintComponent validation

**Choice:** During `Awake()`, the component looks up its own rigid body index (obj1) and each joint's obj2 rigid body index. If obj2 lacks a RigidBodyComponent, that joint is logged as an error and skipped. Invalid joints do not prevent other joints or the simulation from proceeding. If obj1 has no RigidBodyComponent, the component registers no joints at all.

### D7: Separate Lagrange multiplier buffers per joint type

**Choice:** Each joint type gets its own set of SoA Lagrange multiplier buffers managed by `XPBDGpuSolver::Impl`:
- Hinge: `gpu_hinge_aligned_axis_lagrange` (float per constraint), `gpu_hinge_position_lagrange` (float per constraint)
- Fixed: `gpu_fixed_rotation_lagrange` (float per constraint), `gpu_fixed_position_lagrange` (float per constraint)

These buffers are sized to match the joint count from PhysicsScene and recreated when counts change. The clear kernels zero these buffers in one pass (writing one float per thread, no struct access needed).

**Rationale:** Separation of concerns — PhysicsScene owns static joint definitions; the solver owns dynamic simulation state. SoA for lagrange means the clear kernel is a simple `dst[i] = 0` loop, and the accumulate kernel reads only the needed multiplier value rather than parsing a struct.

## GPU Buffer Layout Summary

### PhysicsScene-owned (AoS, read-only during solve)

**GpuFixedJoint** (48 bytes):
```
uint   obj1_index
uint   obj2_index
float  compliance
float  _pad
vec4   initial_rel_pos_local
vec4   initial_rel_rotation
```

**GpuHingeJoint** (80 bytes):
```
uint   obj1_index
uint   obj2_index
float  compliance
float  _pad
vec4   obj1_local_aligned_axis
vec4   obj2_local_aligned_axis
vec4   obj1_local_attach_point
vec4   obj2_local_attach_point
```

### XPBDGpuSolver-owned (SoA, read-write during solve)

| Buffer | Count | Type |
|--------|-------|------|
| `gpu_hinge_aligned_axis_lagrange` | N_hinge | float[] |
| `gpu_hinge_position_lagrange` | N_hinge | float[] |
| `gpu_fixed_rotation_lagrange` | N_fixed | float[] |
| `gpu_fixed_position_lagrange` | N_fixed | float[] |

## Risks / Trade-offs

- **Numerical stability with many constraints**: If a rigid body participates in many joints, delta averaging may slow convergence. → Mitigated by the compliance parameter; user can increase substep iteration count if needed.

- **Shader binding overhead for joint buffers**: Adding separate AoS definition buffers and SoA lagrange buffers increases descriptor set pressure per joint solve dispatch. → Negligible for joints; the pipeline already handles 20+ bindings.

- **FixedJoint with no explicit attach points**: If two bodies are initially far apart, the "attach point" at the COM will drive them together, potentially creating large initial forces. → Not an issue for the intended use case (nearby bodies); compliance softens the constraint.

- **Compatibility with future angle limits**: Removing hinge angle limit fields keeps things simple, but if added later will require additional shader uniform / GPU buffer layout changes. → Acceptable; recompiling shaders and refreshing buffers later is straightforward.

## Open Questions

None — all design choices were resolved during exploration.
