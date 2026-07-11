## Context

The current HingeJoint constraint system was introduced in `2026-06-21-add-gpu-joint-constraints`. The user-facing `HingeJointDef` requires four spatial parameters: `m_obj1_local_aligned_axis`, `m_obj2_local_aligned_axis`, `m_obj1_local_attach_point`, and `m_obj2_local_attach_point`. The user must manually compute obj2-local values that are consistent with obj1-local values and both bodies' world transforms. Any mistake produces a non-zero initial constraint residual, potentially destabilizing the XPBD solver.

FixedJoint, by contrast, requires zero spatial parameters — its `initial_rel_pos_local` and `initial_rel_rotation` are automatically derived from world transforms at `Awake()` time.

The GPU struct `GpuHingeJoint` (80 bytes, 5 × vec4) stores all four local vectors. The shader reads them directly with no derivation step.

## Goals / Non-Goals

**Goals:**
- Eliminate user responsibility for obj2-local parameter consistency — the system derives them
- Unify GPU data model with FixedJoint by storing `initial_rel_*` instead of obj2-local values
- Consistent naming: `hinge_axis` / `hinge_anchor` across the full stack
- Validate and normalize hinge axis at registration time to prevent degenerate constraints
- Keep GPU buffer size unchanged (80 bytes)

**Non-Goals:**
- Angle limits, motors, or any new hinge features
- World-space convenience API for `HingeJointDef`
- SceneBuilder convenience methods
- Changes to FixedJoint or any other constraint type

## Decisions

### Decision 1: Store initial relative transform on GPU, derive obj2-local values in shader (APPROACH B)

**Alternatives considered:**
- **APPROACH A**: Pre-compute obj2-local values on CPU, store all 4 vectors in GPU buffer. Shader reads directly. Rejected because it stores redundant data (obj2 values are derivable from obj1 values + initial relative transform) and misses the opportunity to unify the data model with FixedJoint.
- **APPROACH B (chosen)**: Store `initial_rel_rotation` and `initial_rel_pos_local` (same two fields as `GpuFixedJoint`) plus obj1-local hinge axis and anchor. Shader derives obj2-local values using `quat_inverse` and `quat_rotate`.

**Rationale**: Conceptual clarity — HingeJoint is a FixedJoint with the rotation constraint relaxed along one axis. Both store the same initial relative transform. The extra ~30 GLSL instructions per hinge per substep are negligible for the expected number of joints. The initial relative rotation already present on GPU also lays the foundation for future angle-limit support.

### Decision 2: Obj1-local frame for user-facing parameters

The user provides `m_hinge_axis_obj1` and `m_hinge_anchor_obj1`, both expressed in obj1's local coordinate frame. This is consistent with FixedJoint's `initial_rel_*` semantics (also obj1-local). No world-space API is provided at the `HingeJointDef` level — editors or higher-level helpers can offer that later.

### Decision 3: Axis validation and normalization in Awake()

`Awake()` calls `glm::normalize()` on `m_hinge_axis_obj1`. If the normalized axis length is below `1e-6`, the hinge is rejected with an `SDL_LogError`. The shader retains `normalize()` as a safety net for floating-point drift. This places validation at the interface boundary rather than silently producing degenerate constraints.

### Decision 4: Full-stack terminology rename

All occurrences of `aligned_axis` / `AlignedAxis` → `hinge_axis` / `HingeAxis` and `attach_point` / `AttachPoint` → `hinge_anchor` / `HingeAnchor`. This includes:
- CPU struct fields (`GpuHingeJoint`, `HingeJointDef`)
- Shader struct fields, buffer binding names, and accessor variables
- Solver buffer variable names and `BindBuffer` string arguments
- Debug buffer names in `EnsureBuffer()` calls

### Decision 5: Parameter order — position before rotation

All `Register*Joint()` functions and GPU structs use the order `initial_rel_pos_local` first, `initial_rel_rotation` second. HingeJoint follows this convention for its initial relative transform parameters.

## Risks / Trade-offs

- **[Breaking API change]** `HingeJointDef` removes two fields. Existing code must be updated. → Mitigation: only one consumer exists (`SceneBuilder::AddDoublePendulum()`), change is mechanical.
- **[Shader performance]** Extra `quat_inverse` + 2× `quat_rotate` per hinge per substep. → Mitigation: operations are on constant data, cost is ~30 GLSL instructions, acceptable for expected hinge counts (<100).
- **[Future angle limits]** The initial relative rotation on GPU is a prerequisite for angle-limit constraints that compare current rotation to the initial pose. Having it already available avoids a second buffer or additional uploads later.
