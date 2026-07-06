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

The system SHALL run collision detection in two stages inside each substep loop, after force integration and shape world pose update have completed, and before position constraint solving begins:

1. **Broad-phase** (`SpatialHashBroadDetector::Detect(cb)`): Owns its own RenderGraph. Computes per-shape AABBs, assigns shapes to spatial grid cells, sorts by cell ID, generates candidate collision pairs. Called once per substep, records its RG directly to the command buffer.
2. **Narrow-phase** (`ConvexCollisionDetector::Detect(cb)`): Owns its own RenderGraph. Reads the candidate pair buffer from broad-phase and runs MPR narrow-phase detection. Called once per substep, records its RG directly to the command buffer.

Collision detection SHALL be owned and managed internally by `XpbdGpuSolver` via `SpatialHashBroadDetector` and `ConvexCollisionDetector` instances. Detector lifecycle SHALL follow the `Configure`/`Detect` two-phase pattern: `Configure` in `PreGPUStep`, `Detect` in `GPUStep`.

#### Scenario: Contacts detected via two-stage pipeline

- **WHEN** a box falls from z=5 to z=2 during force integration
- **AND** broad-phase runs after integration, identifying candidate pairs via spatial hash
- **AND** narrow-phase runs after broad-phase, running MPR on each candidate pair
- **THEN** the contact with the floor at z=0 is detected at z=2 (the new position)
- **AND** penetration depth reflects the current overlap

#### Scenario: Detectors record independent RGs in sequence

- **WHEN** `GPUStep(cb)` runs a substep iteration
- **THEN** `broad_detector->Detect(cb)` is called, recording its own RenderGraph to `cb`
- **AND** `narrow_detector->Detect(cb)` is called next, recording its own RenderGraph to `cb`
- **AND** the solver's PostCollisionPreIterRG is recorded after both detectors

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

### Requirement: Velocity update from pose delta

The system SHALL recompute velocities from the net pose change during the substep, using the pre-gravity and post-solve positions and orientations.

Linear velocity SHALL be computed as `v = (p_post - p_pre) / dt`.

Angular velocity SHALL be computed as `dq = q_post * q_pre_inv`, `w = sign(dq.w) * 2 * dq.xyz / dt`.

#### Scenario: Velocity reflects integrated pose change

- **WHEN** a body fell under gravity during the substep (p_post.z < p_pre.z)
- **THEN** the updated linear velocity z-component is negative
- **AND** its magnitude equals the displacement divided by dt

### Requirement: Contact velocity solving with friction

The system SHALL resolve contact velocity constraints (restitution and friction) using the same Jacobi accumulation+apply pattern as the position solve, using the accumulated lagrange multiplier for the friction impulse cap. The velocity solver SHALL compute world-space contact points from shape-local collision output using current body pose and shape local offset, matching the position solver's coordinate conversion. The velocity solver SHALL NOT use `SubstepStartPosition` or `SubstepStartOrientation` for contact point coordinate conversion.

The pre-contact reference velocities (`PreContactLinearVelocity`, `PreContactAngularVelocity`) SHALL be snapshotted **after** force integration (not before), so they include the gravity impulse applied during the current substep. This ensures the restitution reference velocity `vn_prev` correctly reflects the velocity at collision detection time. The lever arms `r_a`, `r_b` SHALL be computed from shape-local contact points using the **current** body orientation.

#### Scenario: Resting contact produces no spurious restitution bounce

- **WHEN** a box rests stably on a flat surface with restitution=0.5
- **AND** pre-contact velocities are snapshotted after force integration
- **AND** gravity is (0, 0, -9.81)
- **THEN** `vn_prev` (pre-contact normal velocity) includes the gravity contribution from this substep
- **AND** `vn` (current normal velocity after position correction) is approximately 0
- **AND** the restitution correction produces near-zero delta velocity
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

### Requirement: Substep and iteration control

The system SHALL support configurable `num_substep_perstep`, `num_iter_persubstep`, and `num_velocity_iters` parameters. Each substep SHALL run force integration (recorded via PreCollisionRG), collision detection (via detector RGs), position solve iterations (via PositionIterRG recorded N times), velocity update (via PostPositionRG), and velocity solve iterations (via VelocityIterRG recorded M times), in that order.

The substep and iteration loops SHALL be expressed as CPU-side loops that call `RecordAllPasses(cb)` on pre-built RGs. Each RG SHALL be built once (lazily, on first frame or when parameters change) and re-recorded the appropriate number of times. Loop iteration counts (`substep_count`, `pos_iters`, `vel_iters`) SHALL NOT trigger RG rebuild.

#### Scenario: Loop RGs re-recorded without rebuild

- **WHEN** `pos_iters` is changed from 1 to 4 in `XpbdConfig`
- **THEN** PositionIterRG is NOT rebuilt
- **AND** `GPUStep` calls `PositionIterRG.RecordAllPasses(cb)` 4 times per substep instead of 1

### Requirement: Contact count guard with fixed dispatch

Per-constraint compute dispatches SHALL use a fixed workgroup count calculated from `max_collision_pairs` (the narrow-phase detector's buffer capacity). Threads with index >= `collision_count` SHALL return immediately.

The per-contact accumulation shaders (`accumulate_contact_position.comp`, `accumulate_contact_velocity.comp`) SHALL be sized for `max_contacts = max_pairs * 5` to accommodate up to 5 contact points per collision pair (4 perturbation + optionally 1 MPR fallback). The `max_pairs` value SHALL be the broad-phase detector's pair buffer capacity.

#### Scenario: Empty contact list dispatched correctly

- **WHEN** `collision_count` is 0
- **THEN** the dispatch still launches `(max_pairs * 5 + 63) / 64` workgroups
- **AND** all threads return immediately at the count guard

#### Scenario: Contact dispatch covers all 5 points per pair

- **WHEN** a collision pair produces 5 contact points (4 perturbation + MPR fallback)
- **AND** `max_pairs` is the broad-phase pair buffer capacity
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

### Requirement: Multi-RG cross-synchronization via prev_access

The `XpbdGpuSolver` SHALL correctly set `prev_access` on every `ImportExternalResource` call in every RG build function, ensuring correct Vulkan pipeline barriers between independently-built RGs recorded in sequence.

For RGs recorded exactly once per phase (non-loop RGs), `prev_access` SHALL reflect the precise access state left by the preceding RG in the sequence. For RGs recorded multiple times in a loop (PositionIterRG, VelocityIterRG), `prev_access` for mutable buffers SHALL use the conservative `{AT::ShaderRandomRead, AT::ShaderRandomWrite}` to cover both the initial external state and the state left by the RG's own previous iteration.

#### Scenario: Loop RG uses conservative prev_access

- **WHEN** PositionIterRG is built
- **AND** it imports `rigid_body_center_world_position` (which it both reads and applies deltas to)
- **THEN** `ImportExternalResource` is called with `prev_access = {AT::ShaderRandomRead, AT::ShaderRandomWrite}`
- **AND** this ensures correct barriers for both the first iteration (after NarrowPhaseDetect leaves the buffer in RR state) and subsequent iterations (after the previous iteration left it in RW state)

#### Scenario: Non-loop RG uses precise prev_access

- **WHEN** PostCollisionPreIterRG is built and imports `rigid_body_center_world_position`
- **THEN** `ImportExternalResource` is called with `prev_access = {AT::ShaderRandomRead}` because the preceding NarrowPhaseDetect only read this buffer
