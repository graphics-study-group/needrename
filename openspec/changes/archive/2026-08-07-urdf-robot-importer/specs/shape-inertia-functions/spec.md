# shape-inertia-functions

## Purpose

Delta spec: `RecalculateRigidBodyState` now optionally skips automatic inertia computation when a manual inertia is provided.

## MODIFIED Requirements

### Requirement: RecalculateRigidBodyState dispatches by shape type

`RecalculateRigidBodyState` SHALL first check `m_rigid_body_use_manual_inertia[rb_index]`. If `true`, the function SHALL copy `m_rigid_body_manual_inertia[rb_index]` into `m_rigid_body_inertia[rb_index]`, compute its inverse into `m_rigid_body_inverse_inertia[rb_index]`, and return without entering the shape-volume accumulation loop. If `false`, the existing dispatch logic (switching on `m_shape_type[shape_index]` for `Box`, `Sphere`, `Cylinder`) SHALL execute unchanged.

The COM position and orientation SHALL still be derived from the GameObject's world transform regardless of the inertia source.

#### Scenario: Manual inertia bypasses shape dispatch
- **WHEN** a rigid body has `m_use_manual_inertia = true` with a known inertia matrix
- **AND** `RecalculateRigidBodyState` is called
- **THEN** the manual inertia is used directly
- **AND** no shape volume or inertia functions are called for that body

#### Scenario: Mixed-shape rigid body center of mass
- **WHEN** a rigid body has a Box (hx=1, hy=1, hz=1, at origin) and a Sphere (r=1, at (3, 0, 0))
- **AND** `RecalculateRigidBodyState` is called
- **THEN** the center of mass is weighted by box volume (8) and sphere volume (~4.189)
- **AND** the COM is closer to the box (larger volume dominates)

#### Scenario: Unknown shape type is skipped
- **WHEN** `m_shape_type[shape_index]` has an unrecognized value (e.g., 99)
- **THEN** `SDL_LogWarn` is emitted
- **AND** the shape contributes zero volume and zero inertia (effectively skipped)
