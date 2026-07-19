# ADR-0006: URDF Importer — PhysicsConstraintComponent on Child Link GO

## Status

Proposed

## Context

The URDF importer (`UrdfLoader::BuildAndSaveSceneAsset`) builds a GameObject
hierarchy from parsed URDF data. For each URDF joint connecting a parent link
to a child link, a `PhysicsConstraintComponent` must be created to enforce the
joint constraint in the physics solver.

The initial implementation placed the constraint component on the **parent**
link GO, with `m_obj2_handle` referencing the child. This required computing:

- `m_hinge_anchor_obj1` — joint origin position in parent's GO-local frame
- `m_hinge_axis_obj1` — joint axis direction in parent's GO-local frame, which
  must be rotated by `joint.origin.rpy` (the rotation from parent link frame to
  child link frame)

The option of placing the constraint on the **child** link GO was identified
during review. In this alternative, `m_hinge_anchor_obj1` is `(0, 0, 0)` (the
anchor IS the child GO's origin) and `m_hinge_axis_obj1` is the URDF axis
converted to engine coordinates directly — no extra rotation needed.

## Decision

**PhysicsConstraintComponent SHALL be placed on the URDF joint's child link
GameObject**, not the parent.

Rationale:

1. **URDF joint semantics map directly to engine constraint fields.** The URDF
   `<joint>` element defines the child link frame's origin relative to the
   parent link frame (`<origin>`), and the joint axis in the joint frame
   (`<axis>`). The joint frame coincides with the child link frame. Placing the
   constraint component on the child GO means:

   - `HingeJointDef::m_hinge_anchor_obj1 = (0, 0, 0)` — the pivot is at the
     child GO origin, which *is* the URDF joint origin
   - `HingeJointDef::m_hinge_axis_obj1 = UrdfAxisToEngine(joint.axis)` — the
     axis is already expressed in the child link frame, the same frame as the
     child GO's local space
   - `m_obj2_handle = parent_link_handle` — the other body is the parent link

   This eliminates the need to transform the axis through `joint.origin.rpy`
   (a bug in the initial implementation) and simplifies the anchor computation.

2. **Each link is at most one joint's child.** A URDF link is the child of
   exactly one joint (the one that positions it), but can be the parent of
   many. Placing the constraint on the child side means each link GO carries at
   most one `PhysicsConstraintComponent` with at most one joint entry. Placing
   it on the parent side requires aggregating all child joints into a single
   component (necessitating the `parent_constraints` map in the current code).

3. **The GPU solver is symmetric.** `accumulate_hinge_position.comp` and
   `accumulate_fixed_position.comp` derive obj2-local values from obj1-local
   values and the initial relative transform. The constraint applies equal and
   opposite forces to both bodies regardless of which is obj1 or obj2.

### Constraints only when both sides have RigidBody

A `PhysicsConstraintComponent` is only created when **both** the child link and
parent link have a `RigidBodyComponent` (i.e., both have `<inertial>` in the
URDF). When one side lacks inertial data, the link without mass is treated as
a collision-only attachment — its shapes are collected by the rigid-body
ancestor through `RigidBodyComponent::CollectShapesRecursivelyAndBind`.

This rule is verified against the A1 URDF: every joint in the kinematic chain
connects two links that both have `<inertial>` elements. Links without
`<inertial>` (e.g., `FR_thigh_shoulder`, `imu_link`) are leaf nodes connected
by fixed joints to a rigid-body parent; their collision shapes merge into the
parent's rigid body.

## Consequences

- `UrdfLoader::BuildAndSaveSceneAsset` removes the `parent_constraints` map and
  instead creates `PhysicsConstraintComponent` on the child link GO per joint.
- `collect_collision_handles` and `add_ignores` for parent-child collision
  filtering continue to work unchanged — both walk the GO subtree recursively.
- The GO→COM conversion in `PhysicsScene` (ADR-0003, D-3) works regardless of
  which side is obj1: `hinge_anchor_com = hinge_anchor_go - c1` with
  `c1 = child_COM_offset` yields the correct COM-local anchor.
- This ADR is specific to the URDF importer. Other systems creating
  `PhysicsConstraintComponent` (e.g., editor, hand-authored prefabs) are not
  affected — they may place the component on whichever GO is convenient.
