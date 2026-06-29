## MODIFIED Requirements

### Requirement: Jacobi contact position solving

The system SHALL resolve contact penetration using XPBD in a Jacobi parallelization pattern across two compute passes: a per-constraint accumulation pass and a per-body application pass.

The per-constraint pass SHALL for each contact point:
1. Read the shape pair from `collision_ids` and map to body indices via `shape_bound_rigid_body`
2. Compute local contact points using the substep-start pose
3. Compute current world contact points by rotating local points with the current pose
4. Compute penetration `C = dot(contact_a - contact_b, normal)` where normal points from B to A
5. If `C <= 0`, skip (already separated)
6. Compute effective mass `w = inv_mass + dot(I_inv * cross(r, n), cross(r, n))`
7. Compute `dlambda = C / (w_a + w_b)` (contact compliance is zero)
8. Atomically accumulate linear and angular position deltas via native `atomicAdd` on `float` SSBOs (using `GL_EXT_shader_atomic_float`), not via the CAS-loop `ATOMIC_ADD_FLOAT` macro on `int` SSBOs
9. Atomically increment each affected body's delta count via `atomicAdd` on `float` SSBOs
10. Accumulate `dlambda` into the per-contact lagrange multiplier (declared as `float v[]`)

The per-body application pass SHALL average the accumulated deltas by count, apply them, then reset accumulators to zero. Kinematic bodies SHALL receive no delta updates. The apply pass SHALL NOT require `intBitsToFloat()` conversions — accumulator values are read directly as floats.

#### Scenario: Two boxes penetrate and are pushed apart

- **WHEN** two dynamic boxes are detected with 0.1 penetration
- **AND** the contact normal points from box B to box A
- **THEN** after one position solve iteration box A moves away from box B along the normal
- **AND** box B moves away from box A in the opposite direction
- **AND** the penetration depth decreases

### Requirement: Contact velocity solving with friction

The system SHALL resolve contact velocity constraints (restitution and friction) using the same Jacobi accumulation+apply pattern as the position solve, using the accumulated lagrange multiplier for the friction impulse cap. The lagrange multiplier SHALL be read directly as a `float` value without `intBitsToFloat()` conversion.

#### Scenario: Sliding box with friction

- **WHEN** a box slides horizontally on a surface with friction=0.5
- **THEN** the horizontal velocity magnitude decreases

### Requirement: Lagrange multiplier lifetime

The per-contact lagrange multiplier buffer (declared as `float v[]`) SHALL be cleared to zero at the start of each substep via a dedicated compute shader. Within a substep, each position-solve iteration SHALL accumulate `dlambda` into the lagrange multiplier via native `atomicAdd`. The velocity-solve phase SHALL read the accumulated lagrange multiplier to compute the friction impulse cap.
