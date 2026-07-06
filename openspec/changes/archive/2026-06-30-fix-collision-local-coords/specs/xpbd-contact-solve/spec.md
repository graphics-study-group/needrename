## MODIFIED Requirements

### Requirement: Jacobi contact position solving

The system SHALL resolve contact penetration using XPBD in a Jacobi parallelization pattern across two compute passes: a per-constraint accumulation pass and a per-body application pass.

The per-constraint pass SHALL for each contact point:
1. Read the shape pair from `collision_ids` and map to body indices via `shape_bound_rigid_body`
2. Read shape-local contact points from `ContactPointA` and `ContactPointB` buffers
3. Read shape local offsets from `ShapeLocalPosition` and `ShapeLocalRotation` buffers
4. Compute the shape-local contact point in body-local space: `local_pt_in_body = quat_rotate(shape_local_rot, local_contact_pt) + shape_local_pos`
5. Compute current world contact points by transforming body-local points with the current body pose: `world_pt = body_pos + quat_rotate(body_rot, local_pt_in_body)`
6. Compute lever arms `r_a = world_pt_a - body_pos_a`, `r_b = world_pt_b - body_pos_b`
7. Compute penetration `C = dot(contact_a - contact_b, normal)` where normal points from B to A
8. If `C <= 0`, skip (already separated)
9. Compute effective mass `w = inv_mass + dot(I_inv * cross(r, n), cross(r, n))`
10. Compute `dlambda = C / (w_a + w_b)` (contact compliance is zero)
11. Atomically accumulate linear and angular position deltas using `atomicAdd` (via `GL_EXT_shader_atomic_float`)
12. Atomically increment each affected body's delta count
13. Accumulate `dlambda` into the per-contact lagrange multiplier

The shader SHALL NOT use `SubstepStartPosition` or `SubstepStartOrientation` for contact point coordinate conversion. Local contact points are read directly from the collision detection output and transformed using only current body pose and shape local offset.

The per-body application pass SHALL average the accumulated deltas by count, apply them, then reset accumulators to zero. Kinematic bodies SHALL receive no delta updates.

#### Scenario: Two boxes penetrate and are pushed apart

- **WHEN** two dynamic boxes are detected with 0.1 penetration
- **AND** the contact normal points from box B to box A
- **AND** shape-local contact points are stored in the collision output buffers
- **THEN** the solver transforms shape-local points to world using current body pose + shape offset
- **AND** after one position solve iteration box A moves away from box B along the normal
- **AND** box B moves away from box A in the opposite direction
- **AND** the penetration depth decreases

#### Scenario: Contact point correctly tracks body rotation across iterations

- **WHEN** a contact constraint is solved across multiple position iterations
- **AND** body A's orientation changes between iterations due to angular correction
- **THEN** the world-space contact point for body A is recomputed using the updated orientation each iteration
- **AND** the lever arm `r_a` reflects the current body pose (not a stale snapshot)

### Requirement: Contact velocity solving with friction

The system SHALL resolve contact velocity constraints (restitution and friction) using the same Jacobi accumulation+apply pattern as the position solve, using the accumulated lagrange multiplier for the friction impulse cap. The velocity solver SHALL compute world-space contact points from shape-local collision output using current body pose and shape local offset, matching the position solver's coordinate conversion. The velocity solver SHALL NOT use `SubstepStartPosition` or `SubstepStartOrientation` for contact point coordinate conversion.

The pre-contact reference velocities (`PreContactLinearVelocity`, `PreContactAngularVelocity`) SHALL be snapshotted **after** force integration (not before), so they include the gravity impulse applied during the current substep. This ensures the restitution reference velocity `vn_prev` correctly reflects the velocity at collision detection time. The lever arms `r_a`, `r_b` SHALL be computed from shape-local contact points using the **current** body orientation.

#### Scenario: Resting contact produces no spurious restitution bounce

- **WHEN** a box rests stably on a flat surface with restitution=0.5
- **AND** pre-contact velocities are snapshotted after force integration
- **AND** gravity is (0, 0, -9.81)
- **THEN** `vn_prev` (pre-contact normal velocity) includes the gravity contribution from this substep
- **AND** `vn` (current normal velocity after position correction) is approximately 0
- **AND** the restitution correction `-vn + min(-restitution * vn_prev, 0)` produces near-zero delta velocity
- **AND** the box does not jitter or bounce

#### Scenario: Sliding box with friction

- **WHEN** a box slides horizontally on a surface with friction=0.5
- **AND** shape-local contact points are used to compute current world-space lever arms
- **THEN** the horizontal velocity magnitude decreases over velocity iterations

#### Scenario: Bouncing box restitution uses post-integration velocities

- **WHEN** a box impacts a surface at high speed with restitution=0.8
- **AND** pre-contact velocities are snapshotted after force integration (including gravity)
- **THEN** the restitution target velocity is computed from the correct impact velocity
- **AND** the bounce magnitude reflects the actual velocity at collision time (not a pre-gravity underestimate)
- **AND** the current contact-point velocity uses lever arms derived from shape-local points and current body pose
