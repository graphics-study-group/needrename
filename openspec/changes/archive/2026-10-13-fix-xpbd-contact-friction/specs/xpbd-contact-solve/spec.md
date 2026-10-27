## MODIFIED Requirements

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

## ADDED Requirements

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
