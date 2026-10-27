# xpbd-contact-solve

## Purpose

Govern the GPU XPBD contact constraint solver: force integration, per-substep collision detection with shape world pose update, Jacobi position solving, velocity update from pose delta, and velocity-level friction + restitution solving. All solver compute passes run via Vulkan compute shaders dispatched through the engine's render graph.

## Requirements

### Requirement: Gravity and external force integration

The system SHALL integrate gravity and external forces on each rigid body using semi-implicit Euler integration in a GPU compute shader. Gravity direction SHALL be configurable via `XpbdConfig::gravity` (default Z-down: `(0, 0, -9.81)`).

Linear integration SHALL compute `v += (gravity + external_force / mass) * dt` followed by `p += v * dt`. For a body marked kinematic (`is_kinematic != 0`) the linear acceleration SHALL be zero, so only the body's authored velocity advances its position.

Angular integration SHALL be performed in the body's local frame: `wb = q_inv * w`, `tb = q_inv * torque - cross(wb, I_local * wb)`, `wb += I_local_inv * tb * dt`, `w = q * wb`, `q += 0.5 * Quat(w, 0) * q * dt` followed by quaternion normalization.

For a kinematic body the force and torque block SHALL be skipped in full, the Coriolis term included: gravity, the external force, the external torque and the Coriolis term SHALL contribute nothing, so neither its linear nor its angular velocity is modified by the integration pass. Its pose SHALL still advance kinematically by those authored velocities alone — the position by the authored linear velocity, the orientation by the authored angular velocity — because a kinematic body's motion is authored, not solved. Zeroing the external torque is not sufficient, because the Coriolis term `-cross(wb, I_local * wb)` is subtracted after the torque is zeroed and is non-zero for any spin about an axis that is not a principal axis of a non-isotropic inertia tensor.

#### Scenario: Gravity pulls a dynamic body downward

- **WHEN** a single dynamic rigid body is above the ground with mass=1, gravity=(0,0,-9.81), dt=0.016
- **THEN** after one integration pass the body's linear velocity z-component changes by -9.81*dt
- **AND** the body's position z-component decreases accordingly

#### Scenario: Kinematic body is not affected by forces

- **WHEN** a body is marked as kinematic (`is_kinematic != 0`)
- **THEN** the integration shader applies no gravity and no external force or torque to it
- **AND** its linear and angular velocities are left exactly as they were by that pass
- **AND** its position and orientation advance by those authored velocities alone

#### Scenario: A spinning kinematic body keeps its spin

- **WHEN** a kinematic body has a non-zero angular velocity about an axis that is not a principal axis of its non-isotropic inertia tensor
- **THEN** the integration pass leaves its angular velocity unchanged
- **AND** the Coriolis term does not turn its spin into a precession

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

1. **Broad-phase** (`SpatialHashBroadDetector::Record(cb)`): Computes per-shape AABBs, assigns shapes to spatial grid cells, sorts by cell ID, generates candidate collision pairs. Called once per substep, records its dispatches directly to the command buffer.
2. **Narrow-phase** (`ConvexCollisionDetector::Record(cb)`): Reads the candidate pair buffer from broad-phase and runs MPR narrow-phase detection. Called once per substep, records its dispatches directly to the command buffer.

Collision detection SHALL be owned and managed internally by `XpbdGpuSolver` via `SpatialHashBroadDetector` and `ConvexCollisionDetector` instances. A detector SHALL prepare itself for the current shape count on the record path — sizing its internal buffers, creating its bindings and configuring itself — so that the solver needs no separate configure step between the substeps of a step or between steps. The solver SHALL obtain the broad detector's pair buffers and pass them to the narrow detector as part of that preparation.

#### Scenario: Contacts detected via two-stage pipeline

- **WHEN** a box falls from z=5 to z=2 during force integration
- **AND** broad-phase runs after integration, identifying candidate pairs via spatial hash
- **AND** narrow-phase runs after broad-phase, running MPR on each candidate pair
- **THEN** the contact with the floor at z=0 is detected at z=2 (the new position)
- **AND** penetration depth reflects the current overlap

#### Scenario: Detectors record independent RGs in sequence

- **WHEN** `GPUStep(cb)` runs a substep iteration
- **THEN** `broad_detector->Record(cb)` is called, recording its dispatches to `cb`
- **AND** `narrow_detector->Record(cb)` is called next, recording its dispatches to `cb`
- **AND** the solver's PostCollisionPreIter passes are recorded after both detectors

#### Scenario: A shape count change needs no configure step

- **WHEN** the shape count differs from the previous step's when a step begins
- **THEN** the detectors prepare themselves for the new count while recording that step
- **AND** the solver does not call a separate configure method before recording

### Requirement: Jacobi contact position solving

The system SHALL resolve contact penetration and tangential drift using XPBD in a Jacobi parallelization pattern across two compute passes: a per-constraint accumulation pass and a per-body application pass.

The per-constraint pass SHALL for each contact point:
1. Read the shape pair from `collision_ids` and map to body indices via `shape_bound_rigid_body`
2. Read shape-local contact points from `ContactPointA` and `ContactPointB` buffers
3. Read shape local offsets from `ShapeLocalPosition` and `ShapeLocalRotation` buffers
4. Compute the shape-local contact point in body-local space: `local_in_body = quat_rotate(shape_local_rot, local_contact_pt) + shape_local_pos`
5. Compute the current world contact points by transforming body-local points with the current body pose: `world_pt = body_pos + quat_rotate(body_rot, local_in_body)`
6. Compute lever arms `r_a = world_pt_a - body_pos_a`, `r_b = world_pt_b - body_pos_b`
7. Compute penetration `C = dot(contact_a - contact_b, normal)`. The normal points from body A toward body B — it is the outward normal of the Minkowski-difference face nearest the origin, which the detector derives as the direction away from its interior point — so `C > 0` in the overlapping configuration the manifold was generated from, and `dot(contact_b - contact_a, normal) < 0` there
8. If `C <= 0`, skip the normal constraint (already separated)
9. Compute the normal inverse effective mass `w_n = inv_mass_a + inv_mass_b + dot(I_inv_a * cross(r_a, n), cross(r_a, n)) + dot(I_inv_b * cross(r_b, n), cross(r_b, n))`, where a kinematic body contributes `inv_mass = 0` and `I_inv = 0`
10. Compute `dlambda_n = C / w_n` (contact compliance is zero) and accumulate it into the per-contact normal multiplier `lambda_n` **before** step 12 reads the static friction budget, so that budget includes this iteration's own normal contribution. Reading the buffer before this write would leave the first position iteration with a zero static budget, and the whole substep's integrated drift would be ineligible for static friction in that iteration
11. Compute the tangential drift of the contact material points **over the whole substep**: evaluate the same body-local points of step 4 at the substep-start pose, `pt_start = substep_start_pos + quat_rotate(substep_start_ori, local_in_body)`, using the `SubstepStartPosition` and `SubstepStartOrientation` buffers, and form `dp = (contact_a - pt_start_a) - (contact_b - pt_start_b)`, `dp_t = dp - dot(dp, n) * n`, and `drift = length(dp_t)`
12. Solve the static friction constraint on that drift. With `t = dp_t / drift` and the tangential inverse effective mass `w_t` computed exactly as in step 9 with `t` in place of `n`, the required multiplier is `dlambda_t = drift / w_t`. It SHALL be applied only if the candidate sum keeps the accumulated tangential multiplier inside the friction cone, `length(lambda_t + dlambda_t * t) <= mu_s * lambda_n`, where `lambda_t` is this substep's accumulated tangential multiplier **vector** for this contact; the applied multiplier SHALL be added to `lambda_t` as that vector, `lambda_t += dlambda_t * t`. If it does not fit, the constraint SHALL be skipped for this iteration so that the accumulated tangential multiplier never leaves the friction cone and the velocity phase's kinetic friction owns the contact — the two phases SHALL NOT both charge friction for the same motion
13. Form the total impulse `impulse = dlambda_n * n + dlambda_t * t` and scatter the linear and angular position deltas for both sides into the solver's sorted-entry scratch buffer at the contact's own entry slot index (`values[channel * entry_capacity + slot]`), writing a contribution flag of 1 for affected sides. The normal and tangential contributions SHALL be summed into one impulse before scattering, because an entry slot is written, never accumulated: the impulse-to-delta map is linear, so one write of the summed impulse is identical to two writes of the separate ones

**The drift reference is the substep-start pose, and it SHALL NOT be the detection-time contact points.** Both points of a detected pair are un-projected from one contact-plane coordinate along one normal, so their tangential offset is *identically zero* at detection; a detection-time reference would therefore measure nothing and would leave the substep's integrated tangential displacement (the force-integration motion that precedes collision detection) uncorrected. Evaluating the same body-local material points at the substep-start pose is what makes the drift cover the whole substep, exactly as the velocity update `v = (p_post - p_substep_start) / dt` covers the whole substep.

The `SubstepStartPosition` and `SubstepStartOrientation` buffers SHALL be used only for this drift reference. They SHALL NOT be used to compute the current contact positions of steps 4-6, which SHALL continue to use the current body pose.

The static friction coefficient SHALL be the pair average `0.5 * (static_friction_a + static_friction_b)` read from `RigidBodyStaticFriction`.

**The accumulated tangential multipliers SHALL be vectors, not sums of magnitudes.** The friction cone bounds a contact's accumulated tangential *impulse*, whose direction is the drift direction `t`. `t` is recomputed from the surviving drift every iteration and rotates whenever the other constraints move either material point, so the accumulated impulse vector and the sum of the per-iteration magnitudes are different quantities: `length(lambda_t)` is at most `sum |dlambda_t|`, by the triangle inequality. Accumulating magnitudes would enforce the strictly stronger condition `sum |dlambda_t| <= mu_s * lambda_n`. It could therefore never leave the cone — the direction of that error is safe — but corrections that cancel each other would still consume the budget, and once the budget were spent the gate would reject every later iteration (`dlambda_t > 0` whenever the contact still drifts), silently dropping a sticking contact's static friction for the rest of the substep while its net tangential impulse was still far inside the cone. Both tangential accumulators SHALL therefore hold one `vec4` per contact point with `.xyz` carrying the vector and `.w` reserved: `ContactTangentLagrange` for the position phase's `lambda_t` and `ContactTangentImpulse` for the velocity phase's `p_friction`. When a contact's drift or sliding direction is constant across its phase's iterations the vector test is identical to the magnitude test, so this is a strict improvement in tightness and not a behaviour change for straight-line sliding. In the velocity phase the vector is additionally the *total* the contact has been given this substep, and the Coulomb bound is applied to that total by projection rather than to the iteration's increment — see the velocity requirement below for why the increment form is strictly stronger than the cone.

A contact (or contact side) that does not contribute this iteration SHALL write nothing at all; its entry slots carry the zeros left by the per-iteration scratch clear. The per-constraint pass SHALL NOT construct entry keys, SHALL NOT look up a permutation map, SHALL NOT read any mode/phase selector, and SHALL NOT clear accumulators.

The per-body application pass SHALL merge the contact, hinge, and fixed partial sums, average them by the total contribution count (the sum of the flag channel), apply them, zero the per-type partial cells it consumed, and leave kinematic bodies without delta updates.

#### Scenario: Two boxes penetrate and are pushed apart

- **WHEN** two dynamic boxes are detected with 0.1 penetration
- **AND** the contact normal points from box A to box B
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

#### Scenario: The drift reference covers the substep's integrated sliding

- **WHEN** a body resting on a slope gains tangential velocity from gravity during force integration and its position advances tangentially before collision detection runs
- **THEN** the contact points are recomputed from the post-integration pose, whose paired tangential offset is zero
- **AND** the drift of step 11 is nevertheless non-zero, because the same body-local points are evaluated at the substep-start pose
- **AND** the static friction correction is what removes that displacement

#### Scenario: A resting body does not creep

- **WHEN** a box rests on a surface whose static friction coefficient holds it
- **AND** the per-substep tangential drift fits the remaining static budget
- **THEN** the position phase applies the drift-cancelling correction
- **AND** the box's tangential position does not drift across substeps
- **AND** the pose-derived tangential velocity is approximately zero, so the velocity phase applies no kinetic friction

#### Scenario: A sliding body is not held by static friction

- **WHEN** a body slides on a surface and the correction required to cancel the drift would leave the friction cone, `length(lambda_t + dlambda_t * t) > mu_s * lambda_n`
- **THEN** the position phase skips the static friction constraint for that contact in that iteration
- **AND** the tangential resistance comes from the velocity phase's kinetic friction, whose coefficient is the dynamic one
- **AND** no tangential multiplier beyond the cone is accumulated

#### Scenario: A rotating drift direction does not exhaust the static budget

- **WHEN** a contact's tangential drift direction changes between position iterations, so the tangential corrections it has been given partially cancel one another
- **AND** the vector sum of those corrections is still inside the cone
- **THEN** the accumulated tangential multiplier is that vector sum, not the sum of the correction magnitudes
- **AND** the contact still receives static friction, because the gate compares the candidate vector sum against `mu_s * lambda_n`
- **AND** a sum-of-magnitudes accumulator would have consumed the budget and skipped the contact for the rest of the substep

#### Scenario: Static friction does not fight the normal correction

- **WHEN** a contact is both penetrating and drifting
- **THEN** the tangential impulse is added to the normal impulse and written once per side
- **AND** the contact's penetration still decreases, because the two contributions are the same projection of a single impulse

#### Scenario: Skipped contacts do not dilute the average

- **WHEN** a body is touched by three contacts in one iteration
- **AND** one of the three has `C <= 0` and contributes nothing
- **THEN** the body's contribution count for that iteration is 2
- **AND** the applied delta equals the sum of the two real contributions divided by 2

### Requirement: Velocity update from pose delta

The system SHALL recompute velocities from the net pose change during the substep, using the pre-gravity and post-solve positions and orientations.

Linear velocity SHALL be computed as `v = (p_post - p_pre) / dt`.

Angular velocity SHALL be computed as `dq = q_post * q_pre_inv`, `w = sign(dq.w) * 2 * dq.xyz / dt`.

#### Scenario: Velocity reflects integrated pose change

- **WHEN** a body fell under gravity during the substep (p_post.z < p_pre.z)
- **THEN** the updated linear velocity z-component is negative
- **AND** its magnitude equals the displacement divided by dt

### Requirement: Contact velocity solving with friction

The system SHALL resolve contact velocity constraints (restitution and kinetic friction) using the same Jacobi accumulation+apply pattern as the position solve, reusing the position phase's sorted entry list and scratch buffer. The velocity solver SHALL compute world-space contact points from shape-local collision output using current body pose and shape local offset, matching the position solver's coordinate conversion.

The pre-contact reference velocities (`PreContactLinearVelocity`, `PreContactAngularVelocity`) SHALL be snapshotted **after** force integration (not before), so they include the gravity impulse applied during the current substep. This ensures the restitution reference velocity `vn_prev` correctly reflects the velocity at collision detection time. The lever arms `r_a`, `r_b` SHALL be computed from shape-local contact points using the **current** body orientation.

**Every constraint direction SHALL use its own effective mass.** The normal and tangential parts of a contact are distinct constraint directions whose inverse effective masses differ whenever the contact has a lever arm (`w(d)` varies with direction through `|r x d|^2 = |r|^2 - (r . d)^2`), so the shader SHALL compute `w_n` with the normal and `w_t` with the tangential direction, and SHALL NOT combine the two corrections into one direction and convert them with a single effective mass. A kinematic body contributes `inv_mass = 0` and `I_inv = 0` to both.

The normal part SHALL:
1. Compute the contact-point relative velocity `rel_vel = (v_a + cross(w_a, r_a)) - (v_b + cross(w_b, r_b))` and `vn = dot(n, rel_vel)`, where the normal points from body A toward body B, so an approaching contact has `vn > 0`
2. Compute the restitution target `normal_delta_v = -vn + min(-restitution * vn_prev, 0)`, with `restitution = restitution_a * restitution_b` and `vn_prev` taken from the pre-contact snapshot at the same lever arms
3. Apply the normal correction only when the position phase actually solved this contact, i.e. only when the accumulated normal multiplier is positive. A contact the position phase skipped (no penetration, or a speculative point inside the contact margin) has no support impulse, and cancelling its normal velocity would remove a genuine approach or separation velocity
4. Convert the target to an impulse with the normal effective mass: `p_n_correction = normal_delta_v / w_n`, applied along `n`

The tangential (kinetic friction) part SHALL:
1. Compute the tangential relative velocity `tangent_vel = rel_vel - vn * n` and its direction `t = tangent_vel / length(tangent_vel)`
2. Compute the normal impulse `p_normal = |lambda_n| / dt` and the kinetic friction bound `mu_d * p_normal`, where `mu_d = 0.5 * (dynamic_friction_a + dynamic_friction_b)`
3. Form the contact's wanted **total** tangential impulse for this substep: the impulse that cancels the remaining sliding, added to what the contact already holds, `p_ideal = p_friction - (length(tangent_vel) / w_t) * t`, where `p_friction` is the substep's accumulated tangential impulse **vector** for this contact and `t = tangent_vel / length(tangent_vel)`. Then clamp that **total** against the bound, **in impulse space**: `p_new = p_ideal` when `length(p_ideal) <= mu_d * p_normal`, otherwise `p_new = p_ideal * (mu_d * p_normal / length(p_ideal))`
4. Scatter the **difference** `p_new - p_friction` and store the total, `p_friction = p_new`. The difference may point against `t`: the total is allowed to unwind and to rotate as the sliding direction turns

**The bound SHALL be applied to the accumulated total, not to the iteration's increment.** Bounding the increment by the cap's remainder is only a *sufficient* condition for `length(p_friction) <= mu_d * p_normal`; by the triangle inequality it is strictly stronger than the cone, and two of its consequences are wrong. The total can then only grow towards the cap and never unwind, so once its length reaches the cap the remainder is zero, every later iteration applies nothing, and the total stays frozen in the direction the first iterations established — even where the cone still admits friction aimed along a sliding direction that has since turned. And because such an increment can only be applied along the current `t`, that frozen direction is what a spinning, rolling, or otherwise turning contact keeps receiving for the rest of the substep.

Projecting the total gives the bound its literal meaning — `length(p_friction) = min(length(p_ideal), mu_d * p_normal)` is at or below `mu_d * p_normal` by construction — and preserves both properties the increment form was chosen for:

- **The correction never reverses the sliding direction.** With `p_stop = length(tangent_vel) / w_t` the impulse that cancels the sliding: when the total is not clamped the scattered difference is exactly `-p_stop * t` and cancels it; when it is clamped, `length(p_ideal) > mu_d * p_normal >= length(p_friction)` implies `p_stop > 2 * dot(p_friction, t)`, and `dot(p_new - p_friction, -t) - p_stop = (dot(p_friction, -t) + p_stop) * (mu_d * p_normal / length(p_ideal) - 1)`, whose factors are respectively positive and negative. Clamping the *increment* needs its own `min` guard to achieve this; the projection subsumes it.
- **The bound holds for the substep, not for the iteration.** The clamped quantity is now literally the total the contact has been given, and because each iteration scatters `p_new - p_friction` and stores `p_new`, those impulses telescope: the substep total is exactly the final `p_friction`.

The kinetic friction correction SHALL be applied to every contact the position phase solved, unconditionally: it SHALL NOT depend on whether the position phase's static constraint was applied or skipped for that contact in that substep. A contact that is sticking has a tangential velocity of approximately zero after the position phase, so this correction is a no-op for it; a contact that is sliding receives up to the full kinetic bound.

The friction bound SHALL hold for the substep, not for the iteration: the accumulated tangential impulse SHALL be projected onto the cone as a whole rather than incremented against it, so that `num_velocity_iters` velocity iterations deliver the same total tangential impulse as one. Re-granting the full bound on every iteration makes friction strength a function of the iteration count.

The shader SHALL apply the normal and tangential impulses as one summed impulse per side, because an entry slot is written, never accumulated, and the impulse-to-delta map is linear.

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
- **AND** the total tangential impulse the contact receives in one substep does not exceed `mu_d * |lambda_n| / dt`

#### Scenario: Friction never reverses the sliding direction

- **WHEN** a contact's tangential velocity is smaller than the impulse the kinetic bound would allow
- **THEN** the applied tangential impulse is capped at the impulse that cancels that velocity
- **AND** the contact does not acquire a tangential velocity opposing its original sliding direction

#### Scenario: A rotating sliding direction does not exhaust the kinetic budget

- **WHEN** a contact's sliding direction rotates across velocity iterations, so its accumulated tangential impulse is not parallel to any single iteration's `t`
- **THEN** `p_friction` is the total the contact has been given, kept inside the cone by projection rather than by bounding each iteration's increment
- **AND** `length(p_friction)` never exceeds `mu_d * |lambda_n| / dt`
- **AND** the total follows the sliding direction as it turns, and can unwind, instead of being frozen at the direction the first iterations established

#### Scenario: Kinetic friction does not abstain

- **WHEN** the position phase skipped a contact's static friction constraint because the drift did not fit the static budget
- **THEN** the velocity phase still applies the kinetic friction correction for that contact
- **AND** the contact's tangential resistance for that substep is bounded by `mu_d * |lambda_n| / dt`, not by zero

#### Scenario: Friction strength does not depend on mass

- **WHEN** two otherwise identical scenes slide a box on a plane with the same friction coefficients
- **AND** one box has mass `m` and the other `100 * m`
- **THEN** both boxes decelerate at the same rate
- **AND** both travel the same distance before stopping

#### Scenario: Friction strength does not depend on the velocity iteration count

- **WHEN** the same sliding scene runs with `num_velocity_iters = 4` and with `num_velocity_iters = 10`
- **THEN** the sliding distance is the same within the test's tolerance
- **AND** the per-substep tangential velocity reduction never exceeds the kinetic bound, in either configuration

#### Scenario: Friction strength does not depend on the sliding direction

- **WHEN** a box slides on a plane along two different horizontal directions
- **AND** the contact lever arms differ between them
- **THEN** the deceleration is the same in both cases, because the tangential impulse is converted with the tangential effective mass rather than with a blended one

#### Scenario: A speculative contact does not cancel the approach velocity

- **WHEN** a body approaches a surface and the manifold contains a point inside the contact margin that is not penetrating
- **AND** the position phase skipped that point (`C <= 0`, so its normal multiplier is zero)
- **THEN** the velocity phase applies no normal correction for that point
- **AND** the body's approach velocity is unchanged, so the contact is solved at the surface rather than at the margin

#### Scenario: A separating contact is not glued

- **WHEN** a body leaves a surface with a separating normal velocity
- **AND** the position phase applied no normal impulse for that contact this substep
- **THEN** the velocity phase does not cancel the separation velocity

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

### Requirement: Solver requires no float-atomic device feature

The XPBD solver's compute shaders SHALL NOT require `GL_EXT_shader_atomic_float`, the `VK_EXT_shader_atomic_float` device extension, or the `shaderBufferFloat32AtomicAdd` feature. All remaining atomic operations used by the solver (sort histograms, pair counters) SHALL be core integer atomics.

#### Scenario: Device without float atomics is accepted

- **WHEN** the engine enumerates a Vulkan 1.3 physical device that lacks `shaderBufferFloat32AtomicAdd`
- **THEN** the device suitability check does not reject it for the solver's needs
- **AND** the XPBD solver runs on that device

#### Scenario: Solver shaders compile without the extension

- **WHEN** all XPBD solver compute shaders are compiled to SPIR-V
- **THEN** no shader declares the `GL_EXT_shader_atomic_float` extension
- **AND** no compiled solver shader declares the `AtomicFloat32AddEXT` capability

### Requirement: Entry construction is a separate pass from delta accumulation

Entry-list construction and per-iteration delta accumulation SHALL be separate compute shaders per constraint type, not two modes of one shader selected by a push constant. The accumulate shaders SHALL NOT carry a mode/phase selector in their push-constant block, and SHALL NOT write an entry-pair buffer.

Each entry pass SHALL be dispatched over the entry **capacity** — one invocation per contact point (writing 2 slots) or per hinge/fixed joint (writing 4 slots) — and SHALL write every slot in `[0, capacity)` on every dispatch. It SHALL NOT return before writing a slot it owns; because the workgroup count rounds up, an invocation whose last slot index is `>= capacity` owns no slots and MAY return immediately (the bound travels in the push-constant block). A slot whose contact or joint does not exist this substep, or whose owning body index is out of range, SHALL be written with the `INVALID` key (`0xFFFFF`).

The entry pass SHALL derive keys from slot **ownership** only and SHALL NOT read body state: it SHALL NOT read rigid body alive flags, kinematic flags, masses, or joint alive flags. Every decision about whether a constraint contributes SHALL live in the accumulate shader that owns it, which SHALL retain its pre-change guard structure (alive-body checks, kinematic checks, inverse-mass checks, `C > 0` and `denom > ε` gates).

The stored slot ids SHALL remain the identity payload (`entry.y`) so the radix sort permutes them for free, and SHALL be the index the segmented reduction reads each entry's values from at level 0.

Each entry pass SHALL additionally establish the substep's **entry count** — the number of leading slots that can carry an owner this substep — and SHALL publish it to the count buffer that the radix sort and the segmented reduction both read at execution time. The published count SHALL be clamped so that no slot at or above the capacity is included, and entries at or beyond the published count SHALL be treated as absent by both the sort and the reduction: the sort skips them, and the segmented reduction SHALL NOT read their values from the per-iteration scratch.

For contacts the count SHALL be derived on the GPU from the collision count (2 slots per contact point), because the collision count is produced on the GPU in the same substep: `min(2 * collision_count, entry_capacity)`. The entry pass SHALL write this value into the count buffer itself; the host SHALL NOT write the contact count. For hinge and fixed joints the count is CPU-known, and the host SHALL write `4 * joint_count` (four slots per joint) before the substep is recorded — the exact number of joint slots, not the capacity derived from it. The write order SHALL NOT matter beyond the barrier that already separates entry construction from the sort.

A slot's key choice is what makes "treated as absent" sound. The pair array's keys are sorted, so the reduction can only recognise a data extent that is also a prefix of the key order: everything at or beyond the count must sort **after** every entry that can contribute, which is what makes "ignore everything from the count on" the same statement as "ignore every entry whose key is at or above `max_key_value`". Contacts get that for free, because the entry pass writes `INVALID` (`0xFFFFF`) for every slot whose contact does not exist. Hinge and fixed joints get it because the published count is the exact joint slot count: the two differ from the capacity only when `joint_count == 0`, and publishing `0` there tells the reduction to ignore the whole group rather than a slot group owned by no joint. Publishing the capacity instead would make the reduction's correctness depend on the entry pass's sentinel choice for slots it never intended anyone to read, which is the kind of cross-component coupling this contract exists to avoid.

The values of slots at or beyond the count are **not** required to be cleared. The count is the reduction's only read bound, so a slot at or beyond it is never read, and the counted clear is bounded the same way.

Keeping the full-capacity write contract while publishing an exact count is deliberate: the count bounds how much data downstream stages read, while the full write keeps every slot's key well-defined for a reader that cannot know the count.

#### Scenario: Entry pass fills the whole capacity even with no live constraints

- **WHEN** a substep has zero contact points, or zero hinge joints
- **THEN** the entry pass is still dispatched with at least one workgroup covering the capacity
- **AND** every entry slot is written with the `INVALID` key
- **AND** no slot retains a pair from an earlier substep

#### Scenario: A disabled joint cannot leave stale entries or phantom deltas

- **WHEN** a hinge joint is disabled (its alive flag becomes zero) after an earlier substep
- **AND** the joint and body counts are unchanged
- **THEN** the hinge entry list is rebuilt that substep and the joint's four slots carry its owner body indices
- **AND** the hinge accumulate shader returns early on the joint's alive guard, writing nothing
- **AND** the joint's slots carry zero scratch values with flag 0 from the per-iteration clear, so neither body receives a delta or an extra count from it

#### Scenario: Body state changes cannot invalidate the entry list

- **WHEN** a body's mass, kinematic flag, or alive flag changes without changing the joint or body counts
- **THEN** the entry keys are unaffected, because they encode only which body owns each slot
- **AND** the accumulate shaders' guards alone decide whether that body receives a contribution this iteration

#### Scenario: Entry count bounds how much the reduction reads

- **WHEN** a substep has fewer live contact points than the contact capacity allows
- **THEN** the published entry count is the number of slots those contacts occupy
- **AND** the radix sort and the segmented reduction read only entries below that count
- **AND** the per-key sums equal those of a reduction over the live contacts alone

#### Scenario: Entry count is clamped to the capacity

- **WHEN** the collision count implies more slots than the contact capacity
- **THEN** the published count is the capacity
- **AND** no slot index at or above the capacity is published

#### Scenario: Zero live constraints publish a zero count

- **WHEN** a substep has zero contact points, or zero hinge joints
- **THEN** the published entry count for that type is zero
- **AND** the reduction writes no per-body partial for that type

#### Scenario: Joint counts are the exact joint slot count

- **WHEN** a substep has `n` hinge joints and `m` fixed joints
- **THEN** the hinge and fixed entry counts are `4 * n` and `4 * m`
- **AND** with `n` (or `m`) greater than zero those counts equal the groups' capacities, so every slot the reduction reads belongs to a joint
- **AND** with `n` (or `m`) equal to zero the count is zero while the group's capacity is clamped to one joint's worth, so the reduction ignores the group entirely instead of reading a slot group owned by no joint

### Requirement: Explicit accumulator clearing

The solver SHALL clear accumulators with explicit `clear_int_buffer.comp` dispatches rather than relying on the accumulate passes to write zeroes. The correctness of an unwritten entry slot SHALL NOT depend on the entry pass having written a particular key for it. The correctness of an entry slot at or beyond the entry count SHALL NOT depend on its value having been cleared.

The per-body partial-sum output buffers (one per constraint type) SHALL be zeroed once before the substep loop. Each per-iteration scratch buffer SHALL be zeroed once per position iteration and once per velocity iteration, immediately before its accumulate pass.

A per-iteration scratch clear SHALL be bounded by that substep's entry count rather than by the entry capacity: it SHALL clear the `num_channels` channel planes for slots below the published count, using the entry capacity as the channel stride. Slots at or above the count SHALL NOT need clearing, because the reduction reads values only for entries below the count.

**The clear's count source and its dispatch geometry SHALL follow the group's count source.**

- For the **contact** group the count is produced on the GPU in the same substep, so the counted clear SHALL read it from a buffer inside the shader at execution time, exactly as the radix sort's count guard does. Its number of workgroups SHALL remain derived from the entry capacity — only the number of elements actually written follows the count. This mirrors the level-0 rule of the segmented reduction: the dispatch geometry follows the capacity, the data extent follows the count.
- For the **hinge** and **fixed** groups the count is known on the CPU, so it SHALL travel in the push-constant block and the number of workgroups SHALL follow the count itself. No count buffer SHALL be bound for those groups.

The contact position and velocity phases SHALL share a single scratch buffer. This is sound because the phases are strictly sequential — every position iteration completes, then velocities are refreshed from the pose, then the velocity iterations run — and each phase clears the buffer immediately before its accumulate pass, so no position-phase value can reach the velocity reduction.

The clearing of the flat per-body and lagrange buffers SHALL keep using `clear_int_buffer.comp`, whose element count is already a push constant. The counted scratch clears SHALL use a separate shader from `clear_int_buffer.comp`, and the CPU-known count source SHALL be a separate shader from the GPU-produced one, so that no shader declares a binding it does not bind and neither job needs a mode flag.

#### Scenario: A body with no contributions keeps zeros

- **WHEN** a body has no contributing constraint of any type in an iteration
- **THEN** its per-type partial cells are zero (cleared by the previous `apply_*` pass, or by the pre-loop clear on the first iteration)
- **AND** its merged contribution count is zero and no delta is applied to it

#### Scenario: A non-contributing constraint leaves no residue

- **WHEN** a contact exists this iteration but its penetration is `C <= 0`
- **THEN** the accumulate pass writes nothing for it
- **AND** its entry slot carries zeros with flag 0 from that iteration's scratch clear
- **AND** it contributes to no body's delta sum and to no body's count

#### Scenario: A scratch clear writes only the slots below the count

- **WHEN** a substep's entry count is far below the entry capacity
- **THEN** each scratch clear writes only the slots below that count
- **AND** the number of cleared elements follows the count, not the capacity
- **AND** for the contact group the number of dispatched workgroups still follows the capacity, because its count is only known on the GPU
- **AND** for the hinge and fixed groups the number of dispatched workgroups follows the count, because their counts are known on the CPU

#### Scenario: Scratch is cleared per iteration, not per substep

- **WHEN** a substep runs 20 position iterations and 20 velocity iterations
- **THEN** each active constraint type records a scratch clear before each of its accumulate passes
- **AND** no scratch clear is recorded only once per substep

### Requirement: Per-substep entry sorting and reuse

Per substep, after collision detection completes, the system SHALL build the contact entry list once (each contact side contributing a `(body, slot)` pair, with unowned slots marked `INVALID`) and sort it by body via the radix sort. The sorted record — a key array plus a payload array holding each entry's own slot id — SHALL be the only derived artifact: no permutation map (`pos_of`) SHALL be built and no inversion pass SHALL be recorded.

All position-solve iterations and all velocity-solve iterations of that substep SHALL reuse that same sorted entry list: accumulation passes SHALL write their per-iteration values at the entry's own slot index in the per-channel scratch array, and the per-body reduction SHALL read the sorted key and payload arrays, taking each key from the key array and gathering each entry's values from the slot its payload carries. Re-sorting SHALL NOT occur per iteration.

The hinge and fixed entry lists SHALL be rebuilt and sorted once per substep as well, with no entry list cached across substeps. The `RadixSort` and `SumByKey` instances SHALL hold no geometry and SHALL serve every constraint type; the per-group buffers SHALL be reallocated only when a body or joint count changes, because their sizes follow the group's capacity.

#### Scenario: Sort runs once per substep, not per iteration

- **WHEN** a substep runs 20 position iterations and 20 velocity iterations
- **THEN** the contact entry list is sorted exactly once during that substep
- **AND** every iteration's accumulation writes into the same slot-indexed scratch layout
- **AND** no permutation inversion dispatch is recorded for the contact list

#### Scenario: Velocity iterations reuse the position entry list

- **WHEN** the velocity-solve phase of a substep begins
- **THEN** no new sort or permutation inversion is recorded for the contact list
- **AND** velocity accumulation scatters into the same slot-indexed layout as position accumulation and reuses the same scratch buffer

#### Scenario: Joint entry lists are rebuilt every substep

- **WHEN** the hinge joint and body counts are unchanged between two substeps
- **THEN** the hinge entry list is still rebuilt and sorted in the second substep
- **AND** no entry list is carried over from the first substep

### Requirement: Lagrange multiplier lifetime

Every constraint type's lagrange multiplier buffers SHALL be cleared to zero at the start of each substep, before the position iteration loop, by that type's dedicated clear shader: the contact multipliers by a flat clear, and the hinge and fixed multipliers by `clear_hinge_lagrange.comp` and `clear_fixed_lagrange.comp`, each of which zeroes both of its type's buffers (`HingeAxisLagrange` and `HingeAnchorLagrange`; `FixedRotationLagrange` and `FixedPositionLagrange`).

The contact phase owns three per-contact accumulators, all per contact point, all sized to the contact-point capacity, and all cleared exactly once per substep:

- `ContactLagrange` — the accumulated normal multiplier `lambda_n`, in force-time units (`kg*m` for a length-valued constraint), written by the position phase and read by the velocity phase
- `ContactTangentLagrange` — the accumulated static friction multiplier vector `lambda_t` (`vec4` per contact point, `.xyz` used), same units as `lambda_n`, written and read by the position phase; its clamp is `mu_s * lambda_n`, so it is a substep budget
- `ContactTangentImpulse` — the accumulated kinetic friction impulse vector `p_friction` (`vec4` per contact point, `.xyz` used), in impulse units (`kg*m/s`), written and read by the velocity phase. Unlike the static multiplier it holds the substep's **total** kinetic friction impulse, because the per-iteration impulses telescope into it; its bound `mu_d * |lambda_n| / dt` is enforced by projecting that total into the cone, so it is a substep budget

Because the multipliers are read-modify-written across iterations, a buffer that is not cleared accumulates across substeps without bound: it must not be possible for any of these buffers to be written by an accumulate pass without having been cleared in the same substep. The two tangential accumulators SHALL be cleared once per substep rather than once per iteration of their phase, because their clamps are substep budgets; clearing them per iteration would reinstate the per-iteration re-granting this change removes.

#### Scenario: Every multiplier buffer is cleared each substep

- **WHEN** a substep runs with contacts, hinge joints and fixed joints present
- **THEN** the contact, contact-tangent-lagrange, contact-tangent-impulse, hinge-axis, hinge-anchor, fixed-rotation and fixed-position multiplier buffers each receive exactly one clear dispatch before the position iterations
- **AND** each clear covers that type's own element count

#### Scenario: No multiplier is accumulated across substeps

- **WHEN** the same constraint is solved in two consecutive substeps with identical geometry
- **THEN** the second substep's first iteration reads a multiplier of zero, not the previous substep's accumulated value
- **AND** the second substep's kinetic friction budget is the full dynamic bound again, not a remainder

#### Scenario: A friction budget is not cleared between iterations

- **WHEN** a substep runs many position iterations and many velocity iterations
- **THEN** neither tangential accumulator is cleared inside either iteration loop
- **AND** the tangential impulse a contact accumulates over the substep is what its clamp bounds

#### Scenario: A joint count of zero is harmless

- **WHEN** a substep runs with no hinge joints (or no fixed joints)
- **THEN** that type's clear dispatches cover zero elements and no out-of-bounds write occurs
- **AND** the buffers remain allocated for the substep's entry list

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

### Requirement: Kinematic bodies are immovable in constraint solving

The system SHALL treat a body marked kinematic as immovable in every constraint computation: its inverse mass SHALL be zero and its inverse inertia SHALL be zero wherever an effective mass, a constraint correction or a friction budget is computed, whatever mass and inertia the body was authored with. This SHALL hold for all four constraint accumulate shaders — `accumulate_contact_position.comp`, `accumulate_contact_velocity.comp`, `accumulate_hinge_position.comp` and `accumulate_fixed_position.comp` — so that a kinematic partner neither softens a contact nor softens a hinge or fixed joint.

The kinematic exception SHALL be established in two places, and both SHALL hold:

- **The solver-facing mirror**: `PhysicsScene` SHALL write zero mass and zero inverse inertia into its rigid-body SoA columns for a body submitted as kinematic, because those columns are the solver's view of the body. The authored values SHALL remain intact in the component and in the descriptor, which are read by the editor, the URDF importer and serialization.
- **The shaders**: each constraint accumulate shader SHALL additionally treat a body whose `is_kinematic` flag is set as immovable, independent of the values it reads, so the constraint semantics do not depend on the upload path having normalized them.

The early-out for a contact whose two bodies are both immovable SHALL follow from the inverse masses being zero, not from testing the authored mass.

#### Scenario: A light kinematic platform does not soften the contact

- **WHEN** a box rests on a kinematic platform whose authored mass is comparable to the box's mass
- **THEN** the effective mass of the contact is the box's alone
- **AND** the penetration correction and the friction budget are not reduced by the platform's authored mass

#### Scenario: A contact at the far end of a large kinematic body behaves like a floor

- **WHEN** a small dynamic body rests near the edge of a large kinematic box, so that the lever arm from that body's center of mass to the contact is long
- **AND** the kinematic box has a non-zero authored inverse inertia
- **THEN** the kinematic body's rotational term contributes nothing to the effective mass
- **AND** the correction does not shrink with the distance from the contact to the kinematic body's center of mass

#### Scenario: A joint to a kinematic body behaves like a joint to the world

- **WHEN** a dynamic body is connected by a hinge or fixed joint to a kinematic body
- **THEN** the joint's denominator is built from the dynamic body alone
- **AND** the joint is not softer than the same joint to a body with infinite mass

#### Scenario: A kinematic-kinematic contact contributes nothing

- **WHEN** two kinematic bodies are in contact
- **THEN** both inverse masses are zero and the shader returns without writing a contribution
- **AND** no lagrange multiplier is accumulated for that contact

#### Scenario: Authored values survive the mirror rule

- **WHEN** a body with `mass = 5` and a computed inertia tensor is submitted with `is_kinematic = true`
- **THEN** the solver-facing mass and inverse-inertia columns for that body are zero
- **AND** the submitted descriptor and the originating component still report the authored mass and inertia

### Requirement: Contact index is bounded by the contact-point capacity

Both contact accumulate shaders SHALL bound their contact index by the contact-point capacity as well as by the published collision count: an invocation SHALL process a contact only when `cidx < collision_count` and `cidx < contact_point_capacity`, where the capacity is `entry_capacity / 2` (two entry slots per contact point) and is available from the push-constant block the shader already receives.

The published collision count SHALL NOT be treated as a capacity bound. A contact index at or above the capacity SHALL write nothing whatsoever — including the normal multiplier accumulation — because the per-contact multiplier buffers are sized to the contact-point capacity and an unconditional `lagrange[cidx] += dlambda` at such an index writes past the end of its buffer. Reading the collision result buffers at such an index is also meaningless: only entries below the contact-write budget are written by the detector, so a higher index reads the previous substep's geometry.

This bound is the enforcing guard; the detector's count is additionally required not to exceed its write budget (see `gpu-convex-collision-detection`), so the two together make a stale or over-budget contact unrepresentable rather than merely harmless.

#### Scenario: An over-budget collision count cannot write past the multiplier buffer

- **WHEN** the detector's collision count exceeds the contact-point capacity
- **THEN** every invocation with an index at or above the capacity returns before touching the collision result buffers or the multiplier buffers
- **AND** no multiplier element beyond the buffer's allocation is written

#### Scenario: An over-budget index does not fabricate a contact

- **WHEN** the collision count exceeds the contact-point capacity and a slot above the budget holds geometry from an earlier substep
- **THEN** that slot is not processed
- **AND** no body receives a delta derived from it
