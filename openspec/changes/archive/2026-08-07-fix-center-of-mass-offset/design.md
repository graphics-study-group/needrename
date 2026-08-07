## Context

Rigid bodies in the engine can have a center of mass (COM) offset from the GameObject's origin, computed automatically via volume-weighted average of attached collision shapes. The `PhysicsScene` stores COM world pose (`center_world_position` / `center_world_rotation`) and the GO-local offset (`center_offset_local_position`).

Three subsystems were found to ignore this offset:

1. **Rendering** — `model_matrix.comp` outputs COM transform; `interface.glsl` composes it with GO-relative local transforms, placing rendered meshes at COM instead of GO position.
2. **Joints** — `PhysicsConstraintComponent::Init` computes joint data from GO world transforms, but XPBD solvers operate on COM positions. The `center_offset_local_position` buffer is uploaded to GPU but never consumed.
3. **Manual inertia** — `RecalculateRigidBodyState` forces `center_offset = 0` when manual inertia is enabled, preventing users from specifying a non-zero COM offset.


## Goals / Non-Goals

**Goals:**
- Fix model matrix to output GO transform (COM pos minus rotated offset)
- Convert joint data from GO-local to COM-local before GPU upload
- Add manual COM offset alongside manual inertia on RigidBodyComponent
- All conversion happens on CPU in PhysicsScene, GPU shaders work entirely in COM space
- Remove dead `Register*Joint` methods

**Non-Goals:**
- No rotational COM offset (only translational offset is supported; COM rotation always equals GO rotation)
- No change to collision detection (shape local-to-COM computation is already correct in `RecalculateRigidBodyState`)
- No change to `update_shape_world_pose.comp` (shapes are already positioned correctly relative to COM)
- No automatic COM re-computation on shape geometry change (existing `EnqueueRigidBodyInitialization` covers this)

## Decisions

### D-1: Joint GO→COM conversion uses pending queues in PhysicsScene

**Chosen**: `PhysicsConstraintComponent::Init` passes GO-local data through `UpdateFixedJoint` / `UpdateHingeJoint` unchanged. These methods store the data in a `pending_joint_updates` queue. `InitializePendingRigidBodies` processes this queue after all `RecalculateRigidBodyState` calls complete.

**Alternatives considered**:
- *Convert in Init after flushing*: Rejected — requires a forced `InitializePendingRigidBodies` call inside Init, breaking Awake/Init separation.
- *Convert in solver*: Rejected — adds complexity to GPU shaders and requires COM offset per body per constraint, duplicating data.

### D-2: Conversion formulas

Given COM offset vectors `c1, c2` in GO-local space, `q1 = GO1_rot`, `q2 = GO2_rot`:

- **FixedJoint**: `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`
- **HingeJoint anchor**: `hinge_anchor_com = hinge_anchor_go - c1`
- **HingeJoint axis**: unchanged (direction, unaffected by translation)
- **Initial rel rotation**: unchanged (COM rotation = GO rotation)

The conversion runs in `PhysicsScene` after `RecalculateRigidBodyState`, using `GetRigidBodyCenterOffsetLocal()` per affected rigid body.

### D-3: Model matrix GPU-side fix

`model_matrix.comp` gains one additional buffer binding (binding 3, `rigid_body_center_offset_local_position`). The output becomes:

```glsl
vec3 go_pos = pos - quat_rotate(quat, center_offset.xyz);
model_matrices.v[index] = mat4(rot[0], rot[1], rot[2], vec4(go_pos, 1.0));
```

No vertex shader or interface.glsl changes needed — `get_model_matrix()` already composes `model_matrices[i] * pc.model` correctly once the matrix represents the GO transform.

### D-4: Manual inertia renamed, COM offset mandatory

`m_use_manual_inertia` → `m_use_manual_inertia_com`. When `true`, user provides:
- `m_manual_inertia_diag` / `m_manual_inertia_offdiag` (unchanged)
- `m_manual_center_of_mass` (new, `glm::vec3`, GO-local)

`RecalculateRigidBodyState` sets both from manual values, skipping automatic computation. Shape local poses are still recomputed relative to the (now manual) COM.

## Risks / Trade-offs

- **Serialization break** — Renamed `m_use_manual_inertia` → `m_use_manual_inertia_com` and new `m_manual_center_of_mass` field mean existing asset files need migration. The reflection system will deserialize the old name as unknown (lost) and the new name as default (false, zero). Impact: any `.asset` with manual inertia will silently revert to automatic computation. Mitigation: documented in proposal; no automatic migration needed since the feature was likely unused in production assets.

- **Model matrix binding slot** — Adding a buffer binding to `model_matrix.comp` shifts the descriptor set layout. The XPBDGpuSolver's `BuildModelMatrixRG()` must be updated to match. Mitigation: the binding is added at the end of the existing layout (binding 3), minimizing disruption.

- **Joint constraint drift** — If COM offset changes between Init and the first `InitializePendingRigidBodies` call (e.g., shape added/removed), the pending joint data was stored with GO-local values that will be converted using the updated COM offset. This is correct behavior — the converted COM-local data reflects the latest COM. No risk.
