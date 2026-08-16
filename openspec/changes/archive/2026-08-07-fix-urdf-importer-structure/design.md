## Context

The current `UrdfLoader::BuildAndSaveSceneAsset` has several structural issues discovered during review:

1. `m_manual_center_of_mass` is never set from `<inertial>/<origin>`, causing COM offset to be zero for all links
2. The revolute joint axis is not rotated through `joint.origin.rpy`, producing incorrect hinge axes for joints with non-zero origin rotation
3. Collision shapes are placed inconsistently — sometimes on the link GO via `m_center`, sometimes on a child GO
4. `PhysicsConstraintComponent` is placed on the parent link GO, requiring unnecessary computation of anchor position and axis rotation
5. Visual geometry is ignored; render uses collision geometry but placed on ad-hoc child GOs

The `fix-center-of-mass-offset` change has already added `m_manual_center_of_mass` to `RigidBodyComponent` and the GO→COM joint conversion pipeline in `PhysicsAdaptor`. The URDF importer was never updated to use it.

## Goals / Non-Goals

**Goals:**
- Set `m_manual_center_of_mass` from URDF `<inertial>/<origin>` for correct COM positioning
- Rotate inertia tensor from URDF inertial frame to link frame when `<inertial>/<origin>` rpy is non-zero
- Restructure GO hierarchy: always create child GOs for collision and visual, with offset encoded in child GO Transform
- Move `PhysicsConstraintComponent` to child link GO, simplifying axis/anchor to direct URDF values
- Only create constraints when both sides have `RigidBodyComponent` (both have `<inertial>`)

**Non-Goals:**
- No changes to `RigidBodyComponent`, `CollisionShapeComponent`, `PhysicsConstraintComponent`, or `PhysicsScene` — these already support the new usage pattern
- No DAE/STL mesh import (Phase 2)
- No changes to parsing logic or coordinate conversion utilities
- No changes to collision filtering logic (subtree traversal is invariant to hierarchy depth)

## Decisions

### D-1: PhysicsConstraintComponent on child link GO

**Choice**: Place `PhysicsConstraintComponent` on the URDF joint's **child** link GO instead of parent. `m_obj2_handle` references the parent.

**Rationale**: The child GO's local frame IS the joint's child link frame. This means:
- `HingeJointDef::m_hinge_anchor_obj1 = (0, 0, 0)` — the pivot is exactly at the child GO's origin, which is the URDF joint origin
- `HingeJointDef::m_hinge_axis_obj1 = UrdfAxisToEngine(joint.axis)` — the axis is already in the child link frame, no rpy rotation needed
- `FixedJointDef` initial relative transform is computed by `PhysicsConstraintComponent::Init` from world transforms, symmetric regardless of which body is obj1

**Alternatives considered**: Keep on parent — requires computing anchor as `UrdfToEnginePos(joint.origin)` and rotating axis by `UrdfRpyToEngineQuat(joint.origin_rpy)`. More complex, more error-prone.

**Cross-ref**: CONTEXT.md § URDF Import Hierarchy.

### D-2: Collision shapes always on child GOs

**Choice**: Unconditionally create a child GO per `<collision>` element. `CollisionShapeComponent` is attached to the child GO with `m_center = 0, m_rotation = 0`. The collision offset is encoded in the child GO's `Transform`.

**Rationale**: The previous code had two code paths (child GO for non-zero offset, direct attachment for zero offset), producing equivalent world-space positions but inconsistent hierarchy. A single path simplifies the code and makes the hierarchy predictable. `m_center = 0` ensures the local-to-world formula in `BuildDescriptor` (`world_center = go_pos + go_rot * m_center`) is a no-op — no double-counting of the offset.

### D-3: Visual child GOs sourced from collision geometry

**Choice**: Create a separate visual child GO per `<collision>` element, with `StaticMeshComponent`. The visual child GO's `Transform` carries the collision origin plus mesh scale.

**Rationale**: Phase 1 design constraint — DAE/STL mesh import is not implemented. Collision primitives (box/sphere/cylinder) provide reliable rendering geometry. The visual child GO is structurally separate from the collision child GO, making the future switch to `<visual>` geometry a simple data source change.

**Alternatives considered**: Merge collision and visual onto the same GO — rejected because the GO transform carries different scale semantics (identity for collision, mesh_scale for visual).

### D-4: Constraint creation gated by RigidBody existence

**Choice**: Only create `PhysicsConstraintComponent` and its joint entries when **both** the parent and child links have `<inertial>` (and therefore `RigidBodyComponent`).

**Rationale**: `PhysicsConstraintComponent::Init` requires both obj1 and obj2 to have valid `RigidBodyComponent` indices. A link without `<inertial>` is collision-only — its collision shapes are collected by the nearest rigid-body ancestor through `RigidBodyComponent::CollectShapesRecursivelyAndBind`. No constraint is needed because the shapes are physically part of the ancestor body.

**Verified**: In the A1 URDF, every joint in the kinematic chain connects two links that both have `<inertial>`. Links without `<inertial>` (`thigh_shoulder`, `imu_link`) are leaf nodes connected by fixed joints to rigid-body parents; their collision shapes merge into the parent.

### D-5: Inertia tensor frame rotation

**Choice**: When `<inertial>/<origin>` has non-zero `rpy`, rotate the inertia tensor from the URDF inertial frame to the link frame: `I_link = Rᵀ * I_inertial * R`, where `R = glm::mat3_cast(UrdfRpyToEngineQuat(inertial.origin_rpy))`.

**Rationale**: The URDF inertia tensor is defined in the inertial frame (rotated relative to the link frame by origin_rpy). The engine's `m_manual_inertia_diag`/`m_manual_inertia_offdiag` are in GO-local space (the link frame). Without rotation, the off-diagonal terms are wrong for non-zero rpy.

The `m_manual_center_of_mass` position vector is NOT affected by origin_rpy — it is always the COM position in link frame coordinates.

### D-6: Sub-GO naming with indices

**Choice**: Name collision child GOs `"{link_name}_collision_{i}"` and visual child GOs `"{link_name}_visual_{i}"` where `i` is the 0-based index of the element.

**Rationale**: The previous code used non-unique names (`_collision`, `_render`) which collide when a link has multiple collision elements. Indexed naming ensures uniqueness.

## Risks / Trade-offs

- **[Risk] Constraint on child changes the constraint data convention** → This is an internal implementation detail of the URDF importer. Other systems creating `PhysicsConstraintComponent` (editor, hand-authored prefabs) are unaffected. The GO→COM conversion in `PhysicsAdaptor` works regardless of which side is obj1.
- **[Risk] Inertia tensor rotation is difficult to verify without non-trivial rpy data** → A1 URDF uses `origin_rpy = (0,0,0)` for all inertial elements. Correctness relies on the math `I_link = Rᵀ * I * R` which is standard tensor transformation. When a URDF with non-zero inertial rpy is imported, the rotated values will be in the serialized asset.
- **[Trade-off] Visual uses collision geometry** → Robot visuals show simple geometric primitives matching collision shapes. Mitigation: Phase 2 switches to `<visual>` data when DAE/STL import is ready, with no structural changes needed.
