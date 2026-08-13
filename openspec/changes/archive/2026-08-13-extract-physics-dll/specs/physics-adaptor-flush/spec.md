# physics-adaptor-flush

## MODIFIED Requirements

### Requirement: Flush converts joints from GO-local to COM-local

For each pending joint descriptor, the Adaptor SHALL convert the GO-local fields to COM-local using `JointConverter`. The conversion SHALL use COM offsets cached from the COM computation step: `c1` (obj1) and `c2` (obj2) in GO-local space, zero if a body was not processed in this Flush.

- Fixed: `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`; `initial_rel_rotation` unchanged
- Hinge: `anchor_com = anchor_go - c1`; `hinge_axis` unchanged (direction vector invariant under translation)

Converted joints SHALL be written into `FixedJointComDescriptor` / `HingeJointComDescriptor` values via `JointConverter::ConvertFixed` / `ConvertHinge` (these types were formerly named `GpuFixedJoint` / `GpuHingeJoint`).

#### Scenario: Fixed joint converted
- **WHEN** a fixed joint has `go_rel_pos = (0, 0, 1)`, `go_rel_rot = identity`, `c1 = (0.2, 0, 0)`, `c2 = (0, 0, 0)`
- **THEN** the COM-local `rel_pos = (-0.2, 0, 1)`

#### Scenario: Hinge joint anchor converted
- **WHEN** a hinge joint has `anchor_go = (0, 0, 0.5)`, `c1 = (0, 0, 0.5)`
- **THEN** `anchor_com = (0, 0, 0)`
