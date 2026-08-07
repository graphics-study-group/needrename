# rigidbody-center-of-mass-offset

## Purpose

Define the center-of-mass (COM) offset handling pipeline: manual COM offset on `RigidBodyComponent`, GO-local to COM-local conversion for joint constraints, and COM offset compensation in the rendering path so meshes render at the GameObject origin while bodies are simulated around their COM.

## Requirements

### Requirement: RigidBodyComponent supports manual center-of-mass offset

`RigidBodyComponent` SHALL provide the following fields for manual rigid body state control:

- `bool m_use_manual_inertia_com` — when `true`, the physics system SHALL skip automatic volume-weighted COM and inertia computation and use the manual values below
- `glm::vec3 m_manual_center_of_mass` — COM offset from the GameObject origin, expressed in GO-local space. Valid only when `m_use_manual_inertia_com` is `true`
- `glm::vec3 m_manual_inertia_diag` — diagonal components (ixx, iyy, izz) of the inertia tensor
- `glm::vec3 m_manual_inertia_offdiag` — off-diagonal components (ixy, ixz, iyz) of the inertia tensor

The former field `m_use_manual_inertia` SHALL be removed and replaced by `m_use_manual_inertia_com`.

#### Scenario: Manual COM offset is set on a rigid body

- **WHEN** `m_use_manual_inertia_com = true`, `m_manual_center_of_mass = (0.5, 1.0, 0.0)`, and manual inertia is provided
- **AND** the rigid body is flushed through `PhysicsAdaptor::Flush`
- **THEN** the computed center offset is `(0.5, 1.0, 0.0)` (GO-local)
- **AND** no automatic volume-weighted COM or inertia computation is performed

#### Scenario: Manual COM offset is zero (COM at GO origin)

- **WHEN** `m_use_manual_inertia_com = true` and `m_manual_center_of_mass = (0, 0, 0)`
- **THEN** the center offset is `(0, 0, 0)` and COM coincides with the GO origin

#### Scenario: Manual COM disabled uses automatic computation

- **WHEN** `m_use_manual_inertia_com = false`
- **THEN** the manual COM and inertia fields are ignored
- **AND** COM is computed via volume-weighted average of attached shapes

### Requirement: PhysicsAdaptor exposes center-of-mass offset

`PhysicsAdaptor` SHALL provide a public getter `glm::vec3 GetComOffsetLocal(uint32_t rigid_body_index) const` that returns the GO-local COM offset for the given rigid body, cached in `m_com_offsets` during `Flush`. Returns `glm::vec3(0.0f)` for unknown indices.

#### Scenario: Valid rigid body returns its COM offset

- **WHEN** `GetComOffsetLocal(i)` is called for a rigid body flushed with a non-zero COM offset
- **THEN** the returned vector matches the cached center offset

#### Scenario: Unknown index returns zero

- **WHEN** `GetComOffsetLocal(idx)` is called for an index not present in `m_com_offsets`
- **THEN** `glm::vec3(0.0f)` is returned

### Requirement: Joint data is converted from GO-local to COM-local before GPU upload

During `PhysicsAdaptor::Flush`, joint submission data SHALL be converted from GO-local to COM-local space using `Internal/JointConverter.hpp` pure functions. For each pending joint, the adaptor SHALL:

1. Read the COM offsets `c1`, `c2` for obj1 and obj2 from `m_com_offsets` (zero if absent)
2. Convert GO-local joint parameters to COM-local:
   - FixedJoint: `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`
   - HingeJoint: `hinge_anchor_com = anchor_go - c1`
   - HingeJoint `hinge_axis`: unchanged (direction vector)
   - `initial_rel_rotation`: unchanged (COM rotation equals GO rotation)
3. Write the converted COM-local values into the final `GpuFixedJoint` or `GpuHingeJoint` via `JointConverter::ConvertFixed` / `JointConverter::ConvertHinge`

`PhysicsConstraintComponent::Init` SHALL pass GO-local values to the adaptor without performing COM conversion.

#### Scenario: FixedJoint with non-zero COM offsets on both bodies

- **WHEN** obj1 has COM offset `c1 = (0.5, 0, 0)`, obj2 has COM offset `c2 = (0, 1, 0)`
- **AND** both have identity rotations at initialization
- **AND** `go_rel_pos = q1_inv * (go2_pos - go1_pos) = (2, 0, 0)`
- **THEN** `com_rel_pos = (2, 0, 0) + I * (0, 1, 0) - (0.5, 0, 0) = (1.5, 1, 0)`

#### Scenario: HingeJoint anchor conversion

- **WHEN** obj1 has COM offset `c1 = (0.3, 0, 0)` and `hinge_anchor_go = (0, 0, 0)` (anchor at GO origin)
- **THEN** `hinge_anchor_com = (0, 0, 0) - (0.3, 0, 0) = (-0.3, 0, 0)`

### Requirement: Renderer compensates COM offset in model matrix

The solver's `model_matrix.comp` SHALL output the COM world transform as the model matrix (no center-offset buffer input). COM-to-GO compensation SHALL happen CPU-side in `RendererComponent::PreRenderUpdate`:

When physics is active and the renderer finds the nearest ancestor registered as a rigid body (`PhysicsAdaptor::FindRigidBodyByObjectHandle`), it SHALL compute the model matrix as `translate(-com_offset) * inverse(rb_world) * model`, where `com_offset = PhysicsAdaptor::GetComOffsetLocal(rigid_idx)` and `model` is the renderer's world transform. This places the rendered mesh at the GameObject origin while the physics body is simulated around its COM.

#### Scenario: Zero COM offset

- **WHEN** `com_offset = (0, 0, 0)`
- **THEN** the composed model matrix position equals the GameObject world position (COM coincides with GO origin)

#### Scenario: Non-zero COM offset

- **WHEN** `com_offset = (2, 0, 0)` in GO-local space, rb world transform is identity, and COM position is `(12, 0, 5)`
- **THEN** the composed model matrix position is `(10, 0, 5)` (GO pos = COM pos - offset)
