# manual-inertia-tensor

## Purpose

Support manually-specified inertia tensors and center-of-mass offsets for rigid bodies: `RigidBodyComponent` exposes serializable manual inertia/COM fields, `RigidBodyDescriptor` carries them into the COM/inertia pipeline, `ComInertiaComputer` respects the manual override, and the URDF importer populates all fields from parsed `<inertial>` data.

## Requirements

### Requirement: RigidBodyComponent stores manual inertia and COM offset fields

`RigidBodyComponent` SHALL expose the following serializable fields for manual rigid body state control:

- `bool m_use_manual_inertia_com` — when `true`, the automatic inertia and COM computation is skipped
- `glm::vec3 m_manual_center_of_mass` — COM offset from the GameObject origin in GO-local space. Valid only when `m_use_manual_inertia_com` is `true`
- `glm::vec3 m_manual_inertia_diag` — diagonal entries of the 3×3 inertia tensor: (ixx, iyy, izz)
- `glm::vec3 m_manual_inertia_offdiag` — off-diagonal entries: (ixy, ixz, iyz)

All fields SHALL default to `false` / zero, preserving existing behavior when not explicitly set.

The URDF importer SHALL populate all four fields from parsed `<inertial>` data:
- `m_manual_center_of_mass` from `<inertial>/<origin>` xyz, converted to engine coordinates
- `m_manual_inertia_diag` and `m_manual_inertia_offdiag` from `<inertial>/<inertia>`, rotated from the URDF inertial frame to the link frame when `<inertial>/<origin>` rpy is non-zero

#### Scenario: Default values preserve existing behavior

- **WHEN** a new `RigidBodyComponent` is created without setting manual inertia fields
- **THEN** `m_use_manual_inertia_com` is `false`
- **AND** inertia and COM are computed automatically as before

#### Scenario: Manual inertia and COM fields are serialized

- **WHEN** a `RigidBodyComponent` with `m_use_manual_inertia_com = true` and custom inertia + COM values is saved to an archive
- **THEN** loading the archive restores all four fields (`m_use_manual_inertia_com`, `m_manual_center_of_mass`, `m_manual_inertia_diag`, `m_manual_inertia_offdiag`) to their saved values

#### Scenario: URDF importer sets COM offset from inertial origin

- **WHEN** the A1 hierarchy is built and the `trunk` link has `<inertial>/<origin xyz="0.0 0.0041 -0.0005">`
- **THEN** `trunk`'s `RigidBodyComponent::m_manual_center_of_mass` equals `(-0.0041, 0.0, -0.0005)` in engine coordinates

#### Scenario: URDF importer rotates inertia tensor for non-zero origin rpy

- **WHEN** a URDF link has `<inertial>/<origin rpy="0 0 1.5708">` (90° around Z) and inertia tensor I
- **THEN** the stored `m_manual_inertia_diag` and `m_manual_inertia_offdiag` reflect `I_link = Rᵀ * I * R` where R is the rotation matrix from the rpy
- **AND** `m_manual_center_of_mass` is the origin xyz converted to engine coordinates (unaffected by the rotation)

### Requirement: RigidBodyDescriptor carries manual inertia state

`RigidBodyDescriptor` SHALL carry the manual inertia and COM fields (`use_manual_inertia_com`, `manual_inertia`, `manual_center_of_mass`) from the component layer to `ComInertiaComputer`. The descriptor is built by `RigidBodyComponent::Init` from the component's reflected fields and submitted to `PhysicsAdaptor` for processing during `Flush`.

#### Scenario: Descriptor carries manual override

- **WHEN** `RigidBodyComponent::Init` builds a `RigidBodyDescriptor` for a component with `m_use_manual_inertia_com = true`
- **THEN** the descriptor's `use_manual_inertia_com` is `true`
- **AND** `manual_inertia` is the assembled 3×3 matrix from `m_manual_inertia_diag` / `m_manual_inertia_offdiag`
- **AND** `manual_center_of_mass` equals `m_manual_center_of_mass`

### Requirement: ComInertiaComputer respects manual inertia flag

`ComInertiaComputer::Compute` SHALL check `rb_desc.use_manual_inertia_com` before performing automatic inertia computation.

When the flag is `true`, the function SHALL:
1. Use `rb_desc.manual_inertia` as the inertia tensor and compute its inverse
2. Set `center_offset_local` to `rb_desc.manual_center_of_mass`
3. Compute the COM world position as `object_world_position + object_world_rotation * manual_com`
4. Skip the volume-weighted center-of-mass and parallel-axis accumulation loop for that rigid body

When the flag is `false`, the existing automatic computation path SHALL execute unchanged.

#### Scenario: Manual inertia skips auto-computation
- **WHEN** `ComInertiaComputer::Compute` is called for a descriptor with `use_manual_inertia_com = true` and a known diagonal matrix I
- **THEN** the output inertia contains I and its inverse
- **AND** `center_offset_local` equals `manual_center_of_mass`
- **AND** the shape-volume accumulation loop is not entered

#### Scenario: Automatic computation unchanged when flag is false
- **WHEN** `ComInertiaComputer::Compute` is called for a descriptor with `use_manual_inertia_com = false`
- **THEN** the existing automatic volume-weighted computation runs identically to before this change
