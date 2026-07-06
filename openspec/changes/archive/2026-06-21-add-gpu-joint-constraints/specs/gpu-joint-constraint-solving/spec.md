# gpu-joint-constraint-solving

## Purpose

Define the XPBD compute shaders that solve hinge and fixed joint constraints in parallel on the GPU, the solver-managed SoA Lagrange multiplier buffers, and the solver-side dispatch integration.

## Requirements

### Requirement: Solver-owned SoA Lagrange multiplier buffers

`XPBDGpuSolver::Impl` SHALL own separate SoA (Structure of Arrays) buffers for Lagrange multiplier runtime state, sized per joint type:

| Buffer | Element count | Description |
|--------|--------------|-------------|
| `gpu_hinge_aligned_axis_lagrange` | N_hinge | float per hinge constraint |
| `gpu_hinge_position_lagrange` | N_hinge | float per hinge constraint |
| `gpu_fixed_rotation_lagrange` | N_fixed | float per fixed constraint |
| `gpu_fixed_position_lagrange` | N_fixed | float per fixed constraint |

These buffers SHALL be lazy-allocated in `EnsureIntermediateBuffers()` and recreated when joint counts change. Each buffer stores one `float` per constraint.

PhysicsScene's joint definition buffers (`gpu_fixed_joints`, `gpu_hinge_joints`) SHALL be read-only during the solve phase and contain only static input data.

#### Scenario: Lagrange buffers sized to joint counts

- **WHEN** PhysicsScene has 5 hinge joints and 3 fixed joints
- **THEN** `gpu_hinge_aligned_axis_lagrange` and `gpu_hinge_position_lagrange` each have 5 floats
- **AND** `gpu_fixed_rotation_lagrange` and `gpu_fixed_position_lagrange` each have 3 floats

#### Scenario: Lagrange buffers recreated on count change

- **WHEN** a new joint is registered between frames
- **THEN** the solver recreates the corresponding lagrange buffers with the new count
- **AND** newly allocated entries are zero-initialized

### Requirement: Hinge joint aligned axis constraint

The system SHALL solve a hinge joint aligned-axis constraint such that the world-space aligned axis of obj1 and obj2 remain parallel. The constraint correction SHALL be computed as:

```
C_align = |cross(axis1_world, axis2_world)|
```

with XPBD compliance tilde_alpha = compliance / (dt²). The correction SHALL produce angular-only impulses applied to both bodies using the Jacobi parallelization pattern (atomic float-add to shared delta/count buffers). The aligned axis lagrange multiplier SHALL be read from and written to the separate `gpu_hinge_aligned_axis_lagrange` buffer.

#### Scenario: Hinge axes are parallel

- **WHEN** `obj1_aligned_axis_global` is parallel to `obj2_aligned_axis_global`
- **THEN** the aligned axis constraint correction (`C_align`) is zero
- **AND** no impulses are applied

#### Scenario: Hinge axes are not parallel

- **WHEN** `obj1_aligned_axis_global` and `obj2_aligned_axis_global` form an angle
- **THEN** an angular correction impulse is computed proportional to the angle
- **AND** the correction is applied as equal-and-opposite angular deltas to both bodies
- **AND** the `aligned_axis_lagrange` value at `constraint_idx` in the SoA buffer is updated

### Requirement: Hinge joint attachment point constraint

The system SHALL solve a hinge joint attachment-point constraint such that the world-space attachment points on obj1 and obj2 coincide. The constraint correction SHALL be computed as:

```
C_pos = |attach_point2_world - attach_point1_world|
```

The correction SHALL produce both linear and angular impulses using the standard lever-arm formula, with XPBD compliance. The position lagrange multiplier SHALL be read from and written to the separate `gpu_hinge_position_lagrange` buffer.

#### Scenario: Attachment points coincide

- **WHEN** `obj1_global_attach_point` equals `obj2_global_attach_point`
- **THEN** the position constraint correction is zero
- **AND** no impulses are applied

#### Scenario: Attachment points are separated

- **WHEN** attachment points are not coincident
- **THEN** a correction impulse is applied along the separation direction
- **AND** both linear and angular deltas are accumulated atomically
- **AND** the `position_lagrange` value at `constraint_idx` in the SoA buffer is updated

### Requirement: Fixed joint rotation constraint

The system SHALL solve a fixed joint rotation constraint that drives the relative rotation between obj1 and obj2 toward its initial value. The constraint correction SHALL be:

```
q_rel_current = q1⁻¹ * q2
q_error = q_rel_current * q_initial⁻¹
C_rot = |2 * q_error.xyz|
```

Using the quaternion vector part scaled by 2 as the angular error. The correction SHALL produce angular-only impulses. The rotation lagrange multiplier SHALL be read from and written to the separate `gpu_fixed_rotation_lagrange` buffer.

#### Scenario: Relative rotation matches initial state

- **WHEN** `q1⁻¹ * q2 * q_initial⁻¹` is the identity quaternion
- **THEN** the rotation constraint correction is zero

#### Scenario: Relative rotation deviates from initial

- **WHEN** the relative rotation differs from the initial snapshot
- **THEN** an angular correction is applied to both bodies
- **AND** the correction direction is along the error quaternion's vector part
- **AND** the `rotation_lagrange` value at `constraint_idx` in the SoA buffer is updated

### Requirement: Fixed joint position constraint

The system SHALL solve a fixed joint position constraint that drives the obj2 COM position in obj1's local frame toward its initial value. The constraint is:

```
C_pos = |pos2_world - pos1_world - quat_rotate(q1, initial_rel_pos_local)|
```

This naturally produces angular correction because the target point rotates with obj1. The position lagrange multiplier SHALL be read from and written to the separate `gpu_fixed_position_lagrange` buffer.

#### Scenario: Relative position matches initial state

- **WHEN** `pos2 - pos1` in obj1's local frame matches `initial_rel_pos_local`
- **THEN** the position constraint correction is zero

#### Scenario: Relative position differs from initial

- **WHEN** obj2 has moved relative to obj1
- **THEN** a correction impulse is applied along the direction from the current to the target position
- **AND** both linear and angular deltas are accumulated atomically
- **AND** the `position_lagrange` value at `constraint_idx` in the SoA buffer is updated

### Requirement: Lagrange multiplier reset before each substep

Before each substep's position iteration loop, all Lagrange multiplier SoA buffers SHALL be reset to zero via dedicated clear compute kernels. Each kernel SHALL write `0.0f` to every element of its target buffer.

The clear kernels SHALL use a simple 1D dispatch (one thread per element, 256 threads per workgroup). The joint definition buffers (PhysicsScene-owned) SHALL NOT be touched by the clear pass.

#### Scenario: Hinge Lagrange multipliers reset

- **WHEN** the clear hinge lagrange kernel dispatches with N_hinge > 0
- **THEN** every element of `gpu_hinge_aligned_axis_lagrange` is set to 0.0
- **AND** every element of `gpu_hinge_position_lagrange` is set to 0.0

#### Scenario: Fixed Lagrange multipliers reset

- **WHEN** the clear fixed lagrange kernel dispatches with N_fixed > 0
- **THEN** every element of `gpu_fixed_rotation_lagrange` is set to 0.0
- **AND** every element of `gpu_fixed_position_lagrange` is set to 0.0

### Requirement: Joint constraints solved during position iteration

The XPBDGpuSolver SHALL dispatch joint constraint accumulate passes during the position constraint iteration loop, after the contact accumulate pass and before the apply body deltas pass. The loop structure SHALL be:

```
for each position iteration:
    1. Clear body delta + count buffers
    2. Accumulate contact position deltas
    3. Accumulate hinge position deltas (if any)
    4. Accumulate fixed position deltas (if any)
    5. Apply body position deltas (once)
```

Each accumulate pass SHALL be a no-op (skip dispatch) if the corresponding joint count is zero.

#### Scenario: No joints present

- **WHEN** both hinge and fixed joint counts are zero
- **THEN** the joint accumulate passes are skipped entirely
- **AND** only contact position solving runs (no change from current behavior)

#### Scenario: Hinge and fixed joints both present

- **WHEN** both joint types have constraint entries
- **THEN** after contact accumulation, hinge accumulation dispatches, then fixed accumulation dispatches
- **AND** all three contribute to the same delta/count buffers atomically
- **AND** apply body position deltas runs once per iteration

### Requirement: Scripted (kinematic) bodies not affected by joint corrections

The joint constraint shaders SHALL check the `rigid_body_is_kinematic` flag for each body. If a body is kinematic, no deltas SHALL be accumulated for that body, but the other body's correction SHALL still be applied.

#### Scenario: Joint between kinematic and dynamic body

- **WHEN** a hinge joint connects a kinematic obj1 and a dynamic obj2
- **THEN** no deltas are applied to obj1
- **AND** full correction deltas are applied to obj2
- **AND** the Lagrange multipliers are still updated

### Requirement: Two-shader pipeline per joint type

Each joint type SHALL have exactly two compute shaders: a lagrange-reset kernel and a position-delta-accumulation kernel. The lagrange-reset kernel SHALL write zeros to the solver-owned SoA lagrange buffers. The accumulation kernel SHALL read joint definition data from PhysicsScene's AoS buffers (read-only), read Lagrange multipliers from the solver's SoA buffers, compute corrections, atomically accumulate deltas into shared body delta buffers, and write updated Lagrange values back to the SoA buffers.

#### Scenario: Accumulation kernel reads joint data and lagrange from separate buffers

- **WHEN** the accumulate hinge position kernel runs
- **THEN** it reads static joint data (axes, attach points, compliance, indices) from the PhysicsScene-owned AoS buffer (read-only)
- **AND** it reads `aligned_axis_lagrange` and `position_lagrange` from the solver-owned SoA buffers
- **AND** it writes updated lagrange values back to the SoA buffers
- **AND** it atomically accumulates body deltas into the shared `LinearPositionDeltaI`, `AngularPositionDeltaI`, and `PositionDeltaCount` buffers
