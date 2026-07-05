## Why

The current HingeJoint API requires the user to specify four spatial parameters across two bodies' local coordinate frames: `obj1_local_aligned_axis`, `obj2_local_aligned_axis`, `obj1_local_attach_point`, and `obj2_local_attach_point`. The user must manually ensure the two local axes map to the same world-space direction and the two local attach points map to the same world-space position. Any inconsistency causes the solver to start with a non-zero constraint residual, potentially destabilizing the simulation. This is error-prone and unnecessary — the system already knows both bodies' world transforms at initialization and can derive obj2-local values automatically, just as FixedJoint derives `initial_rel_*` from world transforms at `Awake()` time.

## What Changes

- **BREAKING**: `HingeJointDef` removes `m_obj2_local_aligned_axis` and `m_obj2_local_attach_point`. Only obj1-local values (`m_hinge_axis_obj1`, `m_hinge_anchor_obj1`) remain.
- `PhysicsConstraintComponent::Awake()` computes the initial relative transform (`initial_rel_pos_local`, `initial_rel_rotation`) from obj1 and obj2 world transforms and passes it to the GPU instead of pre-computed obj2-local vectors.
- `GpuHingeJoint` struct replaces `obj2_local_aligned_axis` and `obj2_local_attach_point` with `initial_rel_pos_local` and `initial_rel_rotation` (identical semantics to `GpuFixedJoint`). Size unchanged at 80 bytes.
- `accumulate_hinge_position.comp` derives obj2-local axis and anchor at runtime from the initial relative transform, using existing `quat_inverse` and `quat_rotate` helpers.
- Full-stack terminology rename: `aligned_axis` → `hinge_axis`, `attach_point` → `hinge_anchor`, across CPU structs, GPU structs, shaders, solver buffers, and debug names.
- `HingeJointDef` axis is validated and normalized in `Awake()`; zero-length axes are rejected with a log error.

## Capabilities

### New Capabilities

- `hinge-joint-constraint`: Complete hinge joint constraint — simplified user API (obj1-local only), GPU data layout using initial relative transform, shader-side obj2-local value derivation, and unified naming convention (`hinge_axis` / `hinge_anchor`) across the full stack.

## Impact

- **CPU**: `HingeJointDef` (PhysicsConstraintComponent.h), `PhysicsConstraintComponent::Awake()` (.cpp), `RegisterHingeJoint()` signature and impl (PhysicsScene.h/.cpp)
- **GPU struct**: `GpuHingeJoint` field layout (PhysicsScene.h)
- **Shader**: `accumulate_hinge_position.comp` — new derivation block + variable reference updates
- **Shader**: `clear_hinge_lagrange.comp` — buffer binding and accessor renames
- **Solver**: Lagrange buffer variable names, `BindBuffer` strings, debug buffer names (XPBDGpuSolver.cpp)
- **Example**: `SceneBuilder::AddDoublePendulum()` adapts to new `HingeJointDef` (SceneBuilder.cpp)
