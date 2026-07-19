## 1. Remove JointSubmitData variant

- [x] 1.1 Delete `using JointSubmitData = std::variant<...>;` from `PhysicsDescriptors.h`

## 2. Split SubmitJoint into typed methods

- [x] 2.1 Replace `void SubmitJoint(uint32_t, const JointSubmitData &)` with `void SubmitFixedJoint(uint32_t, const FixedJointSubmitData &)` and `void SubmitHingeJoint(uint32_t, const HingeJointSubmitData &)` in `PhysicsAdaptor.h`
- [x] 2.2 Implement `SubmitFixedJoint` to store into `m_pending_fixed_joints`, `SubmitHingeJoint` to store into `m_pending_hinge_joints` in `PhysicsAdaptor.cpp`
- [x] 2.3 Update call sites in `PhysicsConstraintComponent.cpp`: `SubmitJoint` → `SubmitFixedJoint` / `SubmitHingeJoint`

## 3. Header: split pending joint maps

- [x] 3.1 Replace `std::unordered_map<uint32_t, JointSubmitData> m_pending_joints{}` with `m_pending_fixed_joints` and `m_pending_hinge_joints` in `PhysicsAdaptor.h`

## 4. Flush: dual-loop joint processing

- [x] 4.1 Update early-return guard in `PhysicsAdaptor::Flush` to check the two new maps
- [x] 4.2 Replace the single joint-conversion loop with two sequential loops: one over `m_pending_fixed_joints` calling `SubmitFixedJoint`, one over `m_pending_hinge_joints` calling `SubmitHingeJoint`, each without `std::visit`
- [x] 4.3 Update the clear step to clear both `m_pending_fixed_joints` and `m_pending_hinge_joints`

## 5. Verification

- [x] 5.1 Build the project and confirm no compilation errors
