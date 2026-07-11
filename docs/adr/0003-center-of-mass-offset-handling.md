# ADR-0003: Center-of-Mass Offset Conversion Architecture

## Status

Accepted

## Context

Rigid bodies in the engine can have a center of mass (COM) offset from the
GameObject's origin. The COM is computed automatically from attached collision
shapes (volume-weighted average), or set manually via RigidBodyComponent.

Several subsystems were found to incorrectly use GameObject (GO) coordinates
where COM coordinates were required:

1. **Model matrix rendering** (`model_matrix.comp`): outputs COM transform as
   the rendering model matrix, but GPU-side composition (`get_model_matrix()`)
   expects the GO transform. The `center_offset_local_position` buffer is
   uploaded to the GPU but never consumed by any shader.

2. **Joint constraints** (`PhysicsConstraintComponent::Init`): computes
   `initial_rel_pos_local`, `hinge_anchor_obj1`, and `hinge_axis_obj1` using GO
   world positions/rotations, but the XPBD solvers operate on COM positions
   (`rigid_body_center_position`). When COM ≠ GO origin, the constraint
   rest-state is incorrectly positioned.

3. **Manual inertia path** (`RecalculateRigidBodyState`): forces
   `center_offset_local_position = 0` when manual inertia is set, preventing
   users from specifying a non-zero COM offset together with a custom inertia
   tensor.

4. **SetRigidBodyTransform**: stores the GO world position into
   `center_world_position` (a placeholder until COM is computed), but the
   naming and lack of documentation obscure the intent. The recovery formula
   `center_world - center_offset` in `RecalculateRigidBodyState` is also
   incorrect for non-zero offsets (missing rotation).

## Decisions

### D-1: Two coordinate spaces — GO-local (component-facing) vs COM-local (physics-facing)

All fields defined on components (`HingeJointDef::m_hinge_anchor_obj1`,
`CollisionShapeComponent::m_center`, `RigidBodyComponent::m_manual_center_of_mass`)
are expressed in **GO-local space** — offsets measured from and rotated by the
GameObject's transform.

All fields stored on the GPU and consumed by physics solvers
(`GpuHingeJoint::hinge_anchor_obj1`, `shape_local_position`,
`initial_rel_pos_local`) are expressed in **COM-local space** — offsets measured
from and rotated by the rigid body's center-of-mass pose.

The conversion GO-local → COM-local is performed on the CPU, inside
`PhysicsScene`, before data reaches GPU buffers.

### D-2: Conversion timing — deferred until after RecalculateRigidBodyState

During `PhysicsConstraintComponent::Init`, COM offsets are not yet available
(`RecalculateRigidBodyState` runs later, inside `InitializePendingRigidBodies`).
Therefore:

1. Joint `Init` data is submitted to `PhysicsScene` in GO-local form, stored
   in a **pending joints queue** (CPU-side, per joint type).
2. `InitializePendingRigidBodies` first runs `RecalculateRigidBodyState` for
   all queued rigid bodies (computing COM positions and offsets).
3. Then it iterates the pending joints queue, performs GO→COM conversion
   using the now-available COM offsets, and writes the final data into the
   joint GPU buffers.

`PhysicsConstraintComponent` is unaware of COM offsets — it only passes
GO-local values through.

### D-3: Conversion formulas

Given COM offset vectors `c1`, `c2` in GO-local space:

- **FixedJoint** initial relative position:
  `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`
  (where `go_rel_pos = q1⁻¹ * (pos2_go - pos1_go)`)

- **HingeJoint** anchor:
  `hinge_anchor_com = hinge_anchor_go - c1`
  (hinge axis is a direction, unchanged by translation offsets)

### D-4: Manual inertia requires manual COM

`RigidBodyComponent::m_use_manual_inertia` is renamed to
`m_use_manual_inertia_com`. When enabled, the user **must** provide both:
- `m_manual_inertia_diag` / `m_manual_inertia_offdiag` (unchanged)
- `m_manual_center_of_mass` (new, `glm::vec3`, in GO-local space)

`RecalculateRigidBodyState` skips automatic volume-weighted computation
entirely and sets COM position, COM offset, and inertia from the manual values.
Shape local poses are still recomputed relative to the (now manual) COM.

### D-5: Model matrix fix — GPU-side

`model_matrix.comp` gains one additional input binding for
`rigid_body_center_offset_local_position`. The output transform becomes:

```
go_pos = com_pos - quat_rotate(com_rot, center_offset_local)
model_matrix = mat4(com_rot, go_pos)
```

This makes the model matrix buffer represent the GO world transform, matching
the expectation of `get_model_matrix()` in the vertex shaders. No CPU-side
buffer or shader interface changes are needed beyond the extra binding.

### D-6: SetRigidBodyTransform documentation

`SetRigidBodyTransform` is documented as storing a **temporary GO-world
placeholder** into `center_world_position`. This value is overwritten by
`RecalculateRigidBodyState` during the next `InitializePendingRigidBodies`
call. The recovery formula in `RecalculateRigidBodyState` is fixed to:

```cpp
GO_pos = center_world - rot(center_rotation, center_offset_local)
```

## Considered Options

### Joint conversion location

- **Component-level** (`PhysicsConstraintComponent::Init`): rejected because
  COM offsets are not yet available at Init time; would require a forced flush
  inside Init, breaking the clean Awake/Init separation.
- **PhysicsScene internal pending queue** (chosen): defers conversion until
  `InitializePendingRigidBodies`, when all COM data is guaranteed fresh.

### Model matrix computation

- **CPU-side GO model matrix buffer**: rejected — requires a new GPU buffer
  and extra CPU→GPU bandwidth for data the GPU can compute trivially.
- **GPU-side correction** (chosen): one-line math change, reuses the already-
  uploaded `center_offset_local_position` buffer.

## Consequences

- `PhysicsScene` gains public getter `GetRigidBodyCenterOffsetLocal(uint32_t)`
  (needed by `InitializePendingRigidBodies` internally, not exposed to components).
- `RegisterFixedJoint` and `RegisterHingeJoint` (unused) are removed.
- `RecalculateRigidBodyState` branch structure changes: manual inertia path now
  sets COM offset from user-provided value instead of zeroing it.
- `model_matrix.comp` descriptor set layout gains one buffer binding.
