## Why

`PhysicsAdaptor` uses a single `unordered_map<uint32_t, JointSubmitData>` (`m_pending_joints`) to buffer pending joint submissions for both fixed and hinge joints. However, fixed and hinge joints each have independent index pools (allocated via separate `AllocateFixedJoint`/`AllocateHingeJoint` methods that return 0-based indices into separate vectors in `PhysicsScene`). When both joint types are present, a hinge joint at index 0 silently overwrites a fixed joint at index 0 in the pending map, causing joint data loss.

## What Changes

- Split `m_pending_joints` into `m_pending_fixed_joints` and `m_pending_hinge_joints`, each typed to its respective submit-data struct
- Split `SubmitJoint` into `SubmitFixedJoint` and `SubmitHingeJoint` — each takes its concrete submit-data type, eliminating the need for `JointSubmitData` variant
- Remove `JointSubmitData` typedef from `PhysicsDescriptors.h`
- Update `Flush` early-return guard and joint-conversion loop to use the two separate maps without variant dispatch
- **BREAKING**: `SubmitJoint` is replaced by two typed methods; callers must use the appropriate one

## Capabilities

### New Capabilities

<!-- None - this is a bug fix with no new capability. -->

### Modified Capabilities

<!-- None - this is a pure implementation fix, no spec-level behavior change. -->

## Impact

- `PhysicsAdaptor.h`: Replace `m_pending_joints` with two type-specific maps; replace `SubmitJoint` with `SubmitFixedJoint` + `SubmitHingeJoint`
- `PhysicsAdaptor.cpp`: Rewrite `SubmitFixedJoint`/`SubmitHingeJoint`, update `Flush` (dual-loop + dual-clear)
- `PhysicsDescriptors.h`: Remove `JointSubmitData` typedef
- `PhysicsConstraintComponent.cpp`: Update two call sites from `SubmitJoint` to typed methods
