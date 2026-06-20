# xpbd-contact-solve

## Purpose

Govern the GPU XPBD contact constraint solver: force integration, per-substep collision detection with shape world pose update, Jacobi position solving, velocity update from pose delta, and velocity-level friction + restitution solving. All solver compute passes run via Vulkan compute shaders dispatched through the engine's render graph.

## Requirements

### Requirement: Gravity and external force integration

The system SHALL integrate gravity and external forces on each rigid body using semi-implicit Euler integration in a GPU compute shader. The integration SHALL skip bodies marked as kinematic (`is_kinematic != 0`). Gravity direction SHALL be configurable via `XpbdConfig::gravity` (default Z-down: `(0, 0, -9.81)`).

Linear integration SHALL compute `v += (gravity + external_force / mass) * dt` followed by `p += v * dt`.

Angular integration SHALL be performed in the body's local frame: `wb = q_inv * w`, `tb = q_inv * torque - cross(wb, I_local * wb)`, `wb += I_local_inv * tb * dt`, `w = q * wb`, `q += 0.5 * Quat(w, 0) * q * dt` followed by quaternion normalization.

#### Scenario: Gravity pulls a dynamic body downward

- **WHEN** a single dynamic rigid body is above the ground with mass=1, gravity=(0,0,-9.81), dt=0.016
- **THEN** after one integration pass the body's linear velocity z-component changes by -9.81*dt
- **AND** the body's position z-component decreases accordingly

#### Scenario: Kinematic body is not affected by forces

- **WHEN** a body is marked as kinematic (`is_kinematic != 0`)
- **THEN** the integration shader skips that body
- **AND** its position and velocity remain unchanged

### Requirement: Shape world pose update

The system SHALL recompute each shape's world-space position and rotation from its owning rigid body's current pose and the shape's local offset, using a dedicated per-shape compute shader dispatched each substep after force integration.

For each alive shape:
- Look up the owning rigid body via `shape_bound_rigid_body`
- If the shape is unbound (`INVALID_INDEX`), copy the shape's local pose directly to world
- Otherwise compute: `world_pos = body_pos + quat_rotate(body_ori, local_pos)`, `world_ori = quat_mul(body_ori, local_ori)`
- Write results to `shape_world_position` and `shape_world_rotation` buffers

The shader SHALL skip dead shapes.

The render graph SHALL declare `UseBuffer` on both the rigid body position/rotation (read) and shape world position/rotation (write) to ensure correct barrier ordering with the preceding force integration and subsequent collision detection passes.

#### Scenario: Shape follows rigid body movement

- **WHEN** a rigid body moves from z=5 to z=4 during force integration
- **AND** its shape has local offset (0, 0, 0)
- **THEN** after the shape world update pass, `shape_world_position.z` equals 4

#### Scenario: Unbound shape keeps its local pose

- **WHEN** a shape has `shape_bound_rigid_body[idx] == INVALID_INDEX`
- **THEN** the shape world update copies `shape_local_position` to `shape_world_position`
- **AND** copies `shape_local_rotation` to `shape_world_rotation`

### Requirement: Per-substep collision detection

The system SHALL run collision detection (pair generation + MPR narrowphase) inside each substep loop, after force integration and shape world pose update have completed, and before position constraint solving begins.

Collision detection SHALL be owned and managed internally by `XPBDGpuSolver` via a `ConvexCollisionDetector` instance created lazily at first Step() call.

#### Scenario: Contacts detected at current positions

- **WHEN** a box falls from z=5 to z=2 during force integration
- **AND** collision detection runs after integration
- **THEN** the contact with the floor at z=0 is detected at z=2 (the new position)
- **AND** penetration depth reflects the current overlap

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
8. Atomically accumulate linear and angular position deltas via `ATOMIC_ADD_FLOAT` macro on int SSBOs
9. Atomically increment each affected body's delta count
10. Accumulate `dlambda` into the per-contact lagrange multiplier

The per-body application pass SHALL average the accumulated deltas by count, apply them, then reset accumulators to zero. Kinematic bodies SHALL receive no delta updates.

#### Scenario: Two boxes penetrate and are pushed apart

- **WHEN** two dynamic boxes are detected with 0.1 penetration
- **AND** the contact normal points from box B to box A
- **THEN** after one position solve iteration box A moves away from box B along the normal
- **AND** box B moves away from box A in the opposite direction
- **AND** the penetration depth decreases

### Requirement: Velocity update from pose delta

The system SHALL recompute velocities from the net pose change during the substep, using the pre-gravity and post-solve positions and orientations.

Linear velocity SHALL be computed as `v = (p_post - p_pre) / dt`.

Angular velocity SHALL be computed as `dq = q_post * q_pre_inv`, `w = sign(dq.w) * 2 * dq.xyz / dt`.

#### Scenario: Velocity reflects integrated pose change

- **WHEN** a body fell under gravity during the substep (p_post.z < p_pre.z)
- **THEN** the updated linear velocity z-component is negative
- **AND** its magnitude equals the displacement divided by dt

### Requirement: Contact velocity solving with friction

The system SHALL resolve contact velocity constraints (restitution and friction) using the same Jacobi accumulation+apply pattern as the position solve, using the accumulated lagrange multiplier for the friction impulse cap.

#### Scenario: Sliding box with friction

- **WHEN** a box slides horizontally on a surface with friction=0.5
- **THEN** the horizontal velocity magnitude decreases

### Requirement: Substep and iteration control

The system SHALL support configurable `num_substep_perstep` and `num_iter_persubstep` parameters. Each substep SHALL run force integration, shape world pose update, collision detection, position solve iterations, velocity update, and velocity solve iterations, in that order.

The substep loop SHALL be expressed as repeated render graph pass declarations. Each iteration within a substep SHALL run the constraint-accumulate and body-apply passes in sequence, with the render graph automatically inserting the necessary barriers.

### Requirement: Contact count guard with fixed dispatch

Per-constraint compute dispatches SHALL use a fixed workgroup count calculated from `max_collision_pairs * 5`. Threads with index >= `collision_count` SHALL return immediately.

The per-contact accumulation shaders (`accumulate_contact_position.comp`, `accumulate_contact_velocity.comp`) SHALL be sized for `max_contacts = max_pairs * 5` to accommodate up to 5 contact points per collision pair (4 perturbation + optionally 1 MPR fallback).

#### Scenario: Empty contact list dispatched correctly

- **WHEN** `collision_count` is 0
- **THEN** the dispatch still launches `(max_pairs * 5 + 63) / 64` workgroups
- **AND** all threads return immediately at the count guard

#### Scenario: Contact dispatch covers all 5 points per pair

- **WHEN** a collision pair produces 5 contact points (4 perturbation + MPR fallback)
- **AND** `max_pairs = N*(N-1)/2`
- **THEN** the dispatch workgroup count is `(max_pairs * 5 + 63) / 64`
- **AND** all 5 contact points from that pair are within dispatch range

### Requirement: Lagrange multiplier lifetime

The per-contact lagrange multiplier buffer SHALL be cleared to zero at the start of each substep via a dedicated compute shader. Within a substep, each position-solve iteration SHALL accumulate `dlambda` into the lagrange multiplier. The velocity-solve phase SHALL read the accumulated lagrange multiplier to compute the friction impulse cap.

### Requirement: Simulation toggle at dispatch time

Each solver compute pass (integration, shape world update, position/velocity solve) SHALL check `PhysicsScene::IsSimulationEnabled()` inside the dispatch lambda, evaluated at render graph execution time each frame. When simulation is paused, these passes SHALL skip dispatch. The model matrix update pass SHALL always run to keep objects visible.

#### Scenario: Space bar pauses and resumes simulation

- **WHEN** the user presses SPACE to toggle simulation off
- **THEN** all solver passes skip dispatch on subsequent frames
- **AND** model matrix update still runs (objects remain visible)
- **WHEN** the user presses SPACE again
- **THEN** solver passes resume dispatching
