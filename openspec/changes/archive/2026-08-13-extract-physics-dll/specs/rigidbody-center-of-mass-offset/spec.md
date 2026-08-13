# rigidbody-center-of-mass-offset

## MODIFIED Requirements

### Requirement: Joint data is converted from GO-local to COM-local before GPU upload

During `PhysicsAdaptor::Flush`, joint submission data SHALL be converted from GO-local to COM-local space using `Internal/JointConverter.hpp` pure functions. For each pending joint, the adaptor SHALL:

1. Read the COM offsets `c1`, `c2` for obj1 and obj2 from `m_com_offsets` (zero if absent)
2. Convert GO-local joint parameters to COM-local:
   - FixedJoint: `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`
   - HingeJoint: `hinge_anchor_com = anchor_go - c1`
   - HingeJoint `hinge_axis`: unchanged (direction vector)
   - `initial_rel_rotation`: unchanged (COM rotation equals GO rotation)
3. Write the converted COM-local values into the final `FixedJointComDescriptor` or `HingeJointComDescriptor` via `JointConverter::ConvertFixed` / `JointConverter::ConvertHinge` (formerly named `GpuFixedJoint` / `GpuHingeJoint`)

`PhysicsConstraintComponent::Init` SHALL pass GO-local values to the adaptor without performing COM conversion.

#### Scenario: FixedJoint with non-zero COM offsets on both bodies

- **WHEN** obj1 has COM offset `c1 = (0.5, 0, 0)`, obj2 has COM offset `c2 = (0, 1, 0)`
- **AND** both have identity rotations at initialization
- **AND** `go_rel_pos = q1_inv * (go2_pos - go1_pos) = (2, 0, 0)`
- **THEN** `com_rel_pos = (2, 0, 0) + I * (0, 1, 0) - (0.5, 0, 0) = (1.5, 1, 0)`

#### Scenario: HingeJoint anchor conversion

- **WHEN** obj1 has COM offset `c1 = (0.3, 0, 0)` and `hinge_anchor_go = (0, 0, 0)` (anchor at GO origin)
- **THEN** `hinge_anchor_com = (0, 0, 0) - (0.3, 0, 0) = (-0.3, 0, 0)`
