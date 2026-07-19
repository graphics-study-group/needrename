# shape-inertia-functions Delta Spec

## MODIFIED Requirements

### Requirement: RecalculateRigidBodyState dispatches by shape type

`RecalculateRigidBodyState` SHALL switch on `m_shape_type[shape_index]` to call the appropriate volume and inertia functions, replacing the hardcoded box calculations. The dispatch SHALL handle `Box` (0), `Sphere` (1), and `Cylinder` (2). Unknown types SHALL be treated as zero-volume (skipped in center-of-mass and inertia accumulation) with an `SDL_LogWarn`.

When `m_rigid_body_use_manual_inertia_com` is `true` for the rigid body, automatic volume-weighted computation SHALL be skipped entirely. The inertia tensor SHALL be set from `m_rigid_body_manual_inertia` and the COM offset SHALL be set from `m_rigid_body_manual_center_of_mass` (provided by `RigidBodyComponent` via a new `SetRigidBodyManualCenterOfMass` method). Shape local poses SHALL still be recomputed relative to the manual COM.

#### Scenario: Mixed-shape rigid body center of mass

- **WHEN** a rigid body has a Box (hx=1, hy=1, hz=1, at origin) and a Sphere (r=1, at (3, 0, 0))
- **AND** `RecalculateRigidBodyState` is called
- **THEN** the center of mass is weighted by box volume (8) and sphere volume (~4.189)
- **AND** the COM is closer to the box (larger volume dominates)

#### Scenario: Unknown shape type is skipped

- **WHEN** `m_shape_type[shape_index]` has an unrecognized value (e.g., 99)
- **THEN** `SDL_LogWarn` is emitted
- **AND** the shape contributes zero volume and zero inertia (effectively skipped)

#### Scenario: Manual inertia and COM override automatic computation

- **WHEN** `m_use_manual_inertia_com = true` with `m_manual_center_of_mass = (0.5, 0, 0)` and manual inertia provided
- **THEN** the COM offset is set to `(0.5, 0, 0)` without volume-weighted computation
- **AND** the inertia tensor is set from the manual values
- **AND** shape local poses are recomputed relative to the manual COM

## RENAMED Requirements

### Requirement: Manual inertia flag renamed
- **FROM**: `m_use_manual_inertia` (bool field on RigidBodyComponent, stored in PhysicsScene as `m_rigid_body_use_manual_inertia`)
- **TO**: `m_use_manual_inertia_com` (bool field on RigidBodyComponent, stored in PhysicsScene as `m_rigid_body_use_manual_inertia_com`)
