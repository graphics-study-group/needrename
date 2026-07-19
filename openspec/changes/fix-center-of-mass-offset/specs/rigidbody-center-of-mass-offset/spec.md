# rigidbody-center-of-mass-offset

## Purpose

Define the center-of-mass (COM) offset handling pipeline: manual COM offset on RigidBodyComponent, GO-local to COM-local conversion for joint constraints, and model matrix COM-to-GO correction for rendering.

## Requirements

### Requirement: RigidBodyComponent supports manual center-of-mass offset

`RigidBodyComponent` SHALL provide the following fields for manual rigid body state control:

- `bool m_use_manual_inertia_com` — when `true`, the physics system SHALL skip automatic volume-weighted COM and inertia computation and use the manual values below
- `glm::vec3 m_manual_center_of_mass` — COM offset from the GameObject origin, expressed in GO-local space. Valid only when `m_use_manual_inertia_com` is `true`
- `glm::vec3 m_manual_inertia_diag` — diagonal components (ixx, iyy, izz) of the inertia tensor (unchanged from prior `m_manual_inertia_diag`)
- `glm::vec3 m_manual_inertia_offdiag` — off-diagonal components (ixy, ixz, iyz) of the inertia tensor (unchanged from prior `m_manual_inertia_offdiag`)

The former field `m_use_manual_inertia` SHALL be removed and replaced by `m_use_manual_inertia_com`.

#### Scenario: Manual COM offset is set on a rigid body

- **WHEN** `m_use_manual_inertia_com = true`, `m_manual_center_of_mass = (0.5, 1.0, 0.0)`, and manual inertia is provided
- **AND** `RecalculateRigidBodyState` is called
- **THEN** `center_offset_local_position` is set to `(0.5, 1.0, 0.0)`
- **AND** `center_world_position` is set to `go_world_pos + rot(go_world_rot, (0.5, 1.0, 0.0))`
- **AND** no automatic volume-weighted COM or inertia computation is performed

#### Scenario: Manual COM offset is zero (COM at GO origin)

- **WHEN** `m_use_manual_inertia_com = true` and `m_manual_center_of_mass = (0, 0, 0)`
- **THEN** `center_offset_local_position` is set to `(0, 0, 0)` and COM coincides with the GO origin

#### Scenario: Manual COM disabled uses automatic computation

- **WHEN** `m_use_manual_inertia_com = false`
- **THEN** the manual COM and inertia fields are ignored
- **AND** `RecalculateRigidBodyState` computes COM via volume-weighted average of attached shapes

### Requirement: PhysicsScene exposes center-of-mass offset

`PhysicsScene` SHALL provide a public getter `glm::vec3 GetRigidBodyCenterOffsetLocal(uint32_t rigid_body_index) const` that returns `center_offset_local_position` (GO-local COM offset) for the given rigid body. Returns `glm::vec3(0.0f)` for invalid or dead indices.

#### Scenario: Valid rigid body returns its COM offset

- **WHEN** `GetRigidBodyCenterOffsetLocal(i)` is called for a valid rigid body with a non-zero COM offset
- **THEN** the returned vector matches the stored `center_offset_local_position`

#### Scenario: Invalid index returns zero

- **WHEN** `GetRigidBodyCenterOffsetLocal(INVALID_INDEX)` is called
- **THEN** `glm::vec3(0.0f)` is returned

### Requirement: Joint data is converted from GO-local to COM-local before GPU upload

During `InitializePendingRigidBodies`, PhysicsScene SHALL, after completing all `RecalculateRigidBodyState` calls, process a pending joint updates queue. For each pending FixedJoint or HingeJoint update, PhysicsScene SHALL:

1. Read the COM offsets `c1`, `c2` for obj1 and obj2 via `GetRigidBodyCenterOffsetLocal`
2. Convert GO-local joint parameters to COM-local:
   - FixedJoint `initial_rel_pos_local`: `com_rel_pos = go_rel_pos + go_rel_rot * c2 - c1`
   - HingeJoint `hinge_anchor_obj1`: `anchor_com = anchor_go - c1`
   - HingeJoint `hinge_axis_obj1`: unchanged (direction vector)
   - `initial_rel_rotation`: unchanged (COM rotation equals GO rotation)
3. Write the converted COM-local values into the final `GpuFixedJoint` or `GpuHingeJoint` in the `m_fixed_joints` / `m_hinge_joints` arrays

`PhysicsConstraintComponent::Init` SHALL pass GO-local values to `UpdateFixedJoint` / `UpdateHingeJoint` without performing COM conversion.

#### Scenario: FixedJoint with non-zero COM offsets on both bodies

- **WHEN** obj1 has COM offset `c1 = (0.5, 0, 0)`, obj2 has COM offset `c2 = (0, 1, 0)`
- **AND** both have identity rotations at initialization
- **AND** `go_rel_pos = q1_inv * (go2_pos - go1_pos) = (2, 0, 0)`
- **THEN** `com_rel_pos = (2, 0, 0) + I * (0, 1, 0) - (0.5, 0, 0) = (1.5, 1, 0)`

#### Scenario: HingeJoint anchor conversion

- **WHEN** obj1 has COM offset `c1 = (0.3, 0, 0)` and `hinge_anchor_go = (0, 0, 0)` (anchor at GO origin)
- **THEN** `hinge_anchor_com = (0, 0, 0) - (0.3, 0, 0) = (-0.3, 0, 0)`

### Requirement: Model matrix represents GameObject world transform

`model_matrix.comp` SHALL read the `rigid_body_center_offset_local_position` buffer (in addition to `rigid_body_center_position` and `rigid_body_center_rotation`). The output model matrix SHALL represent the **GameObject** world transform, not the COM transform:

```glsl
vec3 go_pos = com_pos - quat_rotate(com_rot, center_offset.xyz);
model_matrices.v[index] = mat4(rot[0], rot[1], rot[2], vec4(go_pos, 1.0));
```

Vertex shaders SHALL continue to compute the final model matrix as `model_matrices[i] * pc.model` without modification.

#### Scenario: Model matrix with zero COM offset

- **WHEN** `center_offset = (0, 0, 0)` and COM position is `(10, 0, 5)`
- **THEN** the output model matrix position is `(10, 0, 5)` (GO pos equals COM pos)

#### Scenario: Model matrix with non-zero COM offset

- **WHEN** `center_offset = (2, 0, 0)` in GO-local space, com_rot is identity, and COM position is `(12, 0, 5)`
- **THEN** the output model matrix position is `(12, 0, 5) - (2, 0, 0) = (10, 0, 5)` (GO pos)
