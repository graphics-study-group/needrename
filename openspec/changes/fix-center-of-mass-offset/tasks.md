## 1. PhysicsScene infrastructure

- [x] 1.1 Add `GetRigidBodyCenterOffsetLocal(uint32_t)` public getter to PhysicsScene
- [x] 1.2 Add `SetRigidBodyManualCenterOfMass(uint32_t, const glm::vec3&)` method for storing manual COM offset
- [x] 1.3 Add `m_rigid_body_manual_center_of_mass` storage vector alongside existing `m_rigid_body_manual_inertia`
- [x] 1.4 Rename `m_rigid_body_use_manual_inertia` → `m_rigid_body_use_manual_inertia_com` (vector and all references)
- [x] 1.5 Add pending joint update queues (`m_pending_fixed_joint_updates`, `m_pending_hinge_joint_updates`) to PhysicsScene
- [x] 1.6 Remove unused `RegisterFixedJoint` and `RegisterHingeJoint` methods (declaration + implementation)
- [x] 1.7 Add new binding slot in `model_matrix.comp` for `rigid_body_center_offset_local_position` (buffer binding 3)
- [x] 1.8 Update `XPBDGpuSolver::BuildModelMatrixRG()` to bind the new offset buffer to the model matrix render graph

## 2. RigidBodyComponent changes

- [x] 2.1 Rename `m_use_manual_inertia` → `m_use_manual_inertia_com` in RigidBodyComponent.h
- [x] 2.2 Add `REFL_SER_ENABLE glm::vec3 m_manual_center_of_mass{0.0f}` field to RigidBodyComponent
- [x] 2.3 Update `RigidBodyComponent::Init()` to call `SetRigidBodyManualCenterOfMass` when `m_use_manual_inertia_com` is true
- [x] 2.4 Update `RigidBodyComponent::Init()` to use renamed method `SetRigidBodyManualInertia` (if renamed) or keep existing

## 3. RecalculateRigidBodyState fix

- [x] 3.1 Fix recovery formula at line 735: `center_world - center_offset` → `center_world - rot(center_rotation, center_offset)`
- [x] 3.2 Update manual inertia branch: set `center_offset` from `m_rigid_body_manual_center_of_mass` instead of zero
- [x] 3.3 Update manual inertia branch: compute `center_world_position = GO_pos + rot(GO_rot, manual_center_of_mass)`
- [x] 3.4 Ensure shape local poses still recompute relative to the (now manual) COM

## 4. Joint COM conversion in Init

- [x] 4.1 Modify `UpdateFixedJoint` and `UpdateHingeJoint` to store GO-local data in pending queues instead of directly writing to `m_fixed_joints` / `m_hinge_joints`
- [x] 4.2 Add GO→COM conversion logic in `InitializePendingRigidBodies`: iterate pending queues after all `RecalculateRigidBodyState` calls
- [x] 4.3 Apply FixedJoint conversion formula: `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`
- [x] 4.4 Apply HingeJoint conversion: `hinge_anchor_com = hinge_anchor_go - c1` (axis unchanged)
- [x] 4.5 Write converted COM-local values into final `m_fixed_joints` / `m_hinge_joints` arrays
- [x] 4.6 Clear pending queues after conversion

## 5. Model matrix GPU fix

- [x] 5.1 Add `center_offset_local_position` buffer input to `model_matrix.comp` (binding 3)
- [x] 5.2 Compute `go_pos = com_pos - quat_rotate(com_rot, center_offset.xyz)` in shader main()
- [x] 5.3 Output GO transform instead of COM transform in model matrix
- [x] 5.4 Update `XPBDGpuSolver` to load the updated `model_matrix.comp` SPIR-V

## 6. PhysicsConstraintComponent simplification

- [x] 6.1 Add comment in `PhysicsConstraintComponent::Init` noting that GO-local values are passed through; conversion happens in PhysicsScene
- [x] 6.2 Verify `PhysicsConstraintComponent::Awake()` calls `AllocateFixedJoint` / `AllocateHingeJoint` (no changes needed)
- [x] 6.3 Update GpuHingeJoint and GpuFixedJoint struct comments to state COM-local space

## 7. Documentation

- [x] 7.1 Add clarifying comments to `SetRigidBodyTransform`: stores temporary GO-world placeholder overwritten by Recalculate
- [x] 7.2 Verify ADR `docs/adr/0003-center-of-mass-offset-handling.md` and CONTEXT.md are committed

## 8. Verification

- [x] 8.1 Build project with `cmake --build build` and verify no compilation errors
- [x] 8.2 Run existing physics tests
- [x] 8.3 Verify `model_matrix.comp` SPIR-V recompiles with new binding
