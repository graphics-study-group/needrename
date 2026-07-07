# manual-inertia-tensor

## Purpose

Allow `RigidBodyComponent` to specify a manually-defined 3×3 inertia tensor that overrides the automatic volume-weighted computation in `PhysicsScene`, so externally-provided inertial data (e.g., from URDF) can be used directly.

## ADDED Requirements

### Requirement: RigidBodyComponent stores manual inertia fields

`RigidBodyComponent` SHALL expose the following serializable fields:

- `bool m_use_manual_inertia` — when `true`, the automatic inertia computation is skipped
- `glm::vec3 m_manual_inertia_diag` — diagonal entries of the 3×3 inertia tensor: (ixx, iyy, izz)
- `glm::vec3 m_manual_inertia_offdiag` — off-diagonal entries: (ixy, ixz, iyz)

All three fields SHALL default to `false` / zero, preserving existing behavior when not explicitly set.

#### Scenario: Default values preserve existing behavior
- **WHEN** a new `RigidBodyComponent` is created without setting manual inertia fields
- **THEN** `m_use_manual_inertia` is `false`
- **AND** inertia is computed automatically as before

#### Scenario: Manual inertia fields are serialized
- **WHEN** a `RigidBodyComponent` with `m_use_manual_inertia = true` and custom inertia values is saved to an archive
- **THEN** loading the archive restores all three manual inertia fields to their saved values

### Requirement: PhysicsScene stores per-rigid-body manual inertia state

`PhysicsScene` SHALL maintain parallel arrays `m_rigid_body_use_manual_inertia` (vector of `bool`) and `m_rigid_body_manual_inertia` (vector of `glm::mat3`) with one entry per registered rigid body.

A method `SetRigidBodyManualInertia(uint32_t rb_index, const glm::mat3& inertia)` SHALL set the flag and store the provided inertia tensor for the given rigid body index.

#### Scenario: Set manual inertia for a rigid body
- **WHEN** `SetRigidBodyManualInertia(0, some_inertia)` is called
- **THEN** `m_rigid_body_use_manual_inertia[0]` is `true`
- **AND** `m_rigid_body_manual_inertia[0]` equals the provided inertia matrix

### Requirement: RecalculateRigidBodyState respects manual inertia flag

`PhysicsScene::RecalculateRigidBodyState` SHALL check `m_rigid_body_use_manual_inertia` before performing automatic inertia computation.

When the flag is `true`, the function SHALL:
1. Copy `m_rigid_body_manual_inertia[rb_index]` into `m_rigid_body_inertia[rb_index]` as a `glm::mat4`
2. Compute `m_rigid_body_inverse_inertia[rb_index]` as the inverse of the inertia matrix
3. Skip the volume-weighted center-of-mass and parallel-axis accumulation loop for that rigid body

When the flag is `false`, the existing automatic computation path SHALL execute unchanged.

The COM position and orientation SHALL still be derived from the GameObject's world transform regardless of the inertia source.

#### Scenario: Manual inertia skips auto-computation
- **WHEN** `RecalculateRigidBodyState` is called for a rigid body with `m_use_manual_inertia = true`
- **AND** the inertia is set to a known diagonal matrix I
- **THEN** `m_rigid_body_inertia[rb_index]` contains I in its upper-left 3×3
- **AND** `m_rigid_body_inverse_inertia[rb_index]` contains I⁻¹
- **AND** the shape-volume accumulation loop is not entered

#### Scenario: Automatic computation unchanged when flag is false
- **WHEN** `RecalculateRigidBodyState` is called for a rigid body with `m_use_manual_inertia = false`
- **THEN** the existing automatic inertia computation runs identically to before this change

### Requirement: RigidBodyComponent::Awake propagates manual inertia to PhysicsScene

When `RigidBodyComponent::Awake()` registers a new rigid body and `m_use_manual_inertia` is `true`, the component SHALL:
1. Assemble the 3×3 `glm::mat3` inertia tensor from `m_manual_inertia_diag` and `m_manual_inertia_offdiag`
2. Call `physics_scene->SetRigidBodyManualInertia(m_rigid_body_index, assembled_inertia)`

When `m_use_manual_inertia` is `false`, no additional call SHALL be made (existing behavior).

#### Scenario: Manual inertia propagated during Awake
- **WHEN** a `RigidBodyComponent` with `m_use_manual_inertia = true` awakes in a scene with physics
- **THEN** `SetRigidBodyManualInertia` is called on the `PhysicsScene` with the assembled inertia tensor
