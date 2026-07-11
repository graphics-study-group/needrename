## 1. CPU data structures — HingeJointDef and GpuHingeJoint

- [x] 1.1 Rename `HingeJointDef` fields: `m_obj1_local_aligned_axis` → `m_hinge_axis_obj1`, `m_obj1_local_attach_point` → `m_hinge_anchor_obj1`, remove `m_obj2_local_aligned_axis` and `m_obj2_local_attach_point`. Rename in `PhysicsConstraintComponent.h`.
- [x] 1.2 Rename `GpuHingeJoint` fields: `obj1_local_aligned_axis` → `hinge_axis_obj1`, `obj1_local_attach_point` → `hinge_anchor_obj1`. Replace `obj2_local_aligned_axis` with `initial_rel_rotation` (glm::vec4), replace `obj2_local_attach_point` with `initial_rel_pos_local` (glm::vec4). Rename in `PhysicsScene.h`. Verify `static_assert(sizeof(GpuHingeJoint) == 80)` still passes.
- [x] 2.1 Update `RegisterHingeJoint()` signature in `PhysicsScene.h`: replace 4 spatial params with `(hinge_axis_obj1, hinge_anchor_obj1, initial_rel_pos_local, initial_rel_rotation)`. Parameter order: pos before rot.
- [x] 2.2 Update `RegisterHingeJoint()` implementation in `PhysicsScene.cpp`: construct `GpuHingeJoint` with new field names and initial relative transform values.
- [x] 2.3 Update `Awake()` hinge branch in `PhysicsConstraintComponent.cpp`: resolve obj2 transform, compute `initial_rel_rotation = glm::inverse(q1) * q2` and `initial_rel_pos_local = glm::inverse(q1) * (pos2 - pos1)`, normalize `m_hinge_axis_obj1`, validate non-zero axis (log error + skip if too short), pass initial relative transform to `RegisterHingeJoint()`.
- [x] 3.1 Update `GpuHingeJoint` struct in `accumulate_hinge_position.comp`: rename fields to `hinge_axis_obj1`, `hinge_anchor_obj1`, `initial_rel_rotation`, `initial_rel_pos_local`.
- [x] 3.2 Add obj2-local derivation block before constraint solving: `q_rel_inv = quat_inverse(initial_rel_rotation)`, `obj2_local_axis = quat_rotate(q_rel_inv, hinge_axis_obj1.xyz)`, `obj2_local_anchor = quat_rotate(q_rel_inv, hinge_anchor_obj1.xyz - initial_rel_pos_local.xyz)`.
- [x] 3.3 Replace `constraint.obj2_local_aligned_axis` with `obj2_local_axis` local variable reference, replace `constraint.obj2_local_attach_point` with `obj2_local_anchor` local variable reference.
- [x] 4.1 Rename buffer binding names: `HingeAlignedAxisLagrange` → `HingeAxisLagrange`, `HingePositionLagrange` → `HingeAnchorLagrange`.
- [x] 4.2 Rename buffer accessor names: `hinge_aligned_axis_lagrange` → `hinge_axis_lagrange`, `hinge_position_lagrange` → `hinge_anchor_lagrange`.
- [x] 5.1 Rename solver buffer member variables in `Impl`: `gpu_hinge_aligned_axis_lagrange` → `gpu_hinge_axis_lagrange`, `gpu_hinge_position_lagrange` → `gpu_hinge_anchor_lagrange`.
- [x] 5.2 Update `EnsureBuffer()` debug names: `"XPBD HingeAlignLagrange"` → `"XPBD HingeAxisLagrange"`, `"XPBD HingePosLagrange"` → `"XPBD HingeAnchorLagrange"`.
- [x] 5.3 Update `BindBuffer()` string arguments: `"HingeAlignedAxisLagrange"` → `"HingeAxisLagrange"`, `"HingePositionLagrange"` → `"HingeAnchorLagrange"` (in both clear and accumulate RenderGraph passes).
- [x] 6.1 Rename Lagrange buffer binding names: `HingeAlignedAxisLagrange` → `HingeAxisLagrange`, `HingePositionLagrange` → `HingeAnchorLagrange`.
- [x] 6.2 Rename Lagrange accessor variables: `aligned_axis_lagrange` → `hinge_axis_lagrange`, `position_lagrange` → `hinge_anchor_lagrange`.
- [x] 7.1 Update `AddDoublePendulum()`: rename `kAlignedAxis` → `kHingeAxis`, rename HingeJointDef member assignments to new field names, remove obj2-local axis and anchor assignments.

## 8. Build verification

- [x] 8.1 Run cmake configure and build to verify all C++ and shader compilation passes with no errors.
