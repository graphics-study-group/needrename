# manual-inertia-tensor Delta

## MODIFIED Requirements

### Requirement: ComInertiaComputer respects manual inertia flag

`ComInertiaComputer::Compute` SHALL check `rb_desc.use_manual_inertia_com` before performing automatic inertia computation.

When the flag is `true`, the function SHALL:
1. Use `rb_desc.manual_inertia` as the inertia tensor and compute its inverse through the shared inertia invertibility gate (scale-relative singularity test)
2. Set `center_offset_local` to `rb_desc.manual_center_of_mass`
3. Compute the COM world position as `object_world_position + object_world_rotation * manual_com`
4. Skip the volume-weighted center-of-mass and parallel-axis accumulation loop for that rigid body

A `manual_inertia` that fails the positive-definiteness check (invertible but physically invalid as an inertia tensor) SHALL make the function throw `std::invalid_argument` rather than silently produce a zero inverse or an inverted garbage tensor.

When the flag is `false`, the existing automatic computation path SHALL execute unchanged.

#### Scenario: Manual inertia skips auto-computation
- **WHEN** `ComInertiaComputer::Compute` is called for a descriptor with `use_manual_inertia_com = true` and a known diagonal matrix I
- **THEN** the output inertia contains I and its inverse
- **AND** `center_offset_local` equals `manual_center_of_mass`
- **AND** the shape-volume accumulation loop is not entered

#### Scenario: Small manual inertia is inverted
- **WHEN** a manual inertia of `diag(9.6e-6)` is provided
- **THEN** its inverse is computed (the absolute-epsilon singularity policy does not apply)

#### Scenario: Invalid manual inertia throws
- **WHEN** `ComInertiaComputer::Compute` is called with `use_manual_inertia_com = true` and a negative-definite `manual_inertia`
- **THEN** `std::invalid_argument` is thrown

#### Scenario: Automatic computation unchanged when flag is false
- **WHEN** `ComInertiaComputer::Compute` is called for a descriptor with `use_manual_inertia_com = false`
- **THEN** the existing automatic volume-weighted computation runs identically to before this change
