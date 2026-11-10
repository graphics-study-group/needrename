## ADDED Requirements

### Requirement: Contact entry construction is a separate pass from delta accumulation

Contact entry-list construction and per-iteration delta accumulation SHALL be separate compute shaders, not two modes of one shader selected by a push constant. The contact accumulate shader SHALL NOT carry a mode/phase selector in its push-constant block, and SHALL NOT write an entry-pair buffer.

The contact entry pass SHALL be dispatched over the contact entry **capacity** — one invocation per contact point, writing 2 slots — and SHALL write every slot in `[0, capacity)` on every dispatch. It SHALL NOT return before writing a slot it owns; because the workgroup count rounds up, an invocation whose last slot index is `>= capacity` owns no slots and MAY return immediately (the bound travels in the push-constant block). A slot whose contact does not exist this substep, or whose owning body index is out of range, SHALL be written with the `INVALID` key (`0xFFFFF`).

The contact entry pass SHALL derive keys from slot **ownership** only and SHALL NOT read body state: it SHALL NOT read rigid body alive flags, kinematic flags or masses. Every decision about whether a contact contributes SHALL live in the contact accumulate shader, which SHALL retain its pre-change guard structure (alive-body checks, kinematic checks, inverse-mass checks, `C > 0` and `denom > ε` gates).

Hinge and fixed joints SHALL have no entry list, no entry pass and no published entry count: they are solved by the joint color passes, which read the joint descriptors through their color bucket list.

The stored slot ids SHALL remain the identity payload (`entry.y`) so the radix sort permutes them for free, and SHALL be the index the segmented reduction reads each entry's values from at level 0.

The contact entry pass SHALL additionally establish the substep's **entry count** — the number of leading slots that can carry an owner this substep — and SHALL publish it to the count buffer that the radix sort and the segmented reduction both read at execution time. The published count SHALL be clamped so that no slot at or above the capacity is included, and entries at or beyond the published count SHALL be treated as absent by both the sort and the reduction: the sort skips them, and the segmented reduction SHALL NOT read their values from the per-iteration scratch.

The contact count SHALL be derived on the GPU from the collision count (2 slots per contact point), because the collision count is produced on the GPU in the same substep: `min(2 * collision_count, entry_capacity)`. The entry pass SHALL write this value into the count buffer itself; the host SHALL NOT write the contact count. The write order SHALL NOT matter beyond the barrier that already separates entry construction from the sort.

A slot's key choice is what makes "treated as absent" sound. The pair array's keys are sorted, so the reduction can only recognise a data extent that is also a prefix of the key order: everything at or beyond the count must sort **after** every entry that can contribute, which is what makes "ignore everything from the count on" the same statement as "ignore every entry whose key is at or above `max_key_value`". Contacts get that for free, because the entry pass writes `INVALID` (`0xFFFFF`) for every slot whose contact does not exist.

The values of slots at or beyond the count are **not** required to be cleared. The count is the reduction's only read bound, so a slot at or beyond it is never read, and the counted clear is bounded the same way.

Keeping the full-capacity write contract while publishing an exact count is deliberate: the count bounds how much data downstream stages read, while the full write keeps every slot's key well-defined for a reader that cannot know the count.

#### Scenario: Contact entry pass fills the whole capacity even with no live contacts

- **WHEN** a substep has zero contact points
- **THEN** the entry pass is still dispatched with at least one workgroup covering the capacity
- **AND** every entry slot is written with the `INVALID` key
- **AND** no slot retains a pair from an earlier substep

#### Scenario: Contact entry count bounds how much the reduction reads

- **WHEN** a substep has fewer live contact points than the contact capacity allows
- **THEN** the published entry count is the number of slots those contacts occupy
- **AND** the radix sort and the segmented reduction read only entries below that count
- **AND** the per-key sums equal those of a reduction over the live contacts alone

#### Scenario: Contact entry count is clamped to the capacity

- **WHEN** the collision count implies more slots than the contact capacity
- **THEN** the published count is the capacity
- **AND** no slot index at or above the capacity is published

#### Scenario: Zero live contacts publish a zero count

- **WHEN** a substep has zero contact points
- **THEN** the published contact entry count is zero
- **AND** the reduction writes no per-body partial for contacts

#### Scenario: A disabled joint cannot leave a stale entry or a phantom delta

- **WHEN** a hinge joint is disabled (its alive flag becomes zero) after an earlier substep
- **THEN** the joint has no entry list to go stale, because joints are reached only through the color bucket list
- **AND** the joint is absent from every color pass and writes no body pose
- **AND** neither body receives a delta from it

#### Scenario: Body state changes cannot invalidate the contact entry list

- **WHEN** a body's mass or alive flag changes without changing the joint or body counts
- **THEN** the contact entry keys are unaffected, because they encode only which body owns each slot
- **AND** the contact accumulate shader's guards alone decide whether that body receives a contribution this iteration

#### Scenario: Joint counts are the exact live joint count

- **WHEN** a substep has `n` hinge joints and `m` fixed joints
- **THEN** the hinge and fixed groups publish no entry list and no entry count
- **AND** the color bucket list of each type holds exactly `n` and `m` slot ids, so no color pass reads a joint that does not exist

### Requirement: Per-substep contact entry sorting and reuse

Per substep, after collision detection completes, the system SHALL build the contact entry list once (each contact side contributing a `(body, slot)` pair, with unowned slots marked `INVALID`) and sort it by body via the radix sort. The sorted record — a key array plus a payload array holding each entry's own slot id — SHALL be the only derived artifact: no permutation map (`pos_of`) SHALL be built and no inversion pass SHALL be recorded.

All position-solve iterations and all velocity-solve iterations of that substep SHALL reuse that same sorted entry list: accumulation passes SHALL write their per-iteration values at the entry's own slot index in the per-channel scratch array, and the per-body reduction SHALL read the sorted key and payload arrays, taking each key from the key array and gathering each entry's values from the slot its payload carries. Re-sorting SHALL NOT occur per iteration.

Hinge and fixed joints SHALL NOT be entered into the radix sort and SHALL NOT have a sorted entry list; their solve order is the color order published by the joint color bucket list, which is rebuilt only when the joint graph changes.

The `RadixSort` and `SumByKey` instances SHALL hold no geometry and SHALL serve the contact group; the group's buffers SHALL be reallocated only when a body or contact count changes, because their sizes follow the group's capacity.

#### Scenario: Contact sort runs once per substep, not per iteration

- **WHEN** a substep runs 20 position iterations and 20 velocity iterations
- **THEN** the contact entry list is sorted exactly once during that substep
- **AND** every iteration's accumulation writes into the same slot-indexed scratch layout
- **AND** no permutation inversion dispatch is recorded for the contact list

#### Scenario: Velocity iterations reuse the position entry list

- **WHEN** the velocity-solve phase of a substep begins
- **THEN** no new sort or permutation inversion is recorded for the contact list
- **AND** velocity accumulation scatters into the same slot-indexed layout as position accumulation and reuses the same scratch buffer

#### Scenario: No joint entry list is built or sorted

- **WHEN** a substep runs with hinge and fixed joints present
- **THEN** no joint entry pass, joint radix sort or joint entry count is recorded
- **AND** the joint passes read the joint descriptors through the color bucket index list instead

#### Scenario: Joint color buckets are not rebuilt every substep

- **WHEN** the hinge joint and body counts are unchanged between two substeps
- **THEN** no joint entry list exists to rebuild and no sort is recorded for joints
- **AND** the color assignment and bucket list are reused, because they change only when the joint graph changes

## MODIFIED Requirements

### Requirement: Jacobi contact position solving

The system SHALL resolve contact penetration and tangential drift using XPBD in a Jacobi parallelization pattern across two compute passes: a per-constraint accumulation pass and a per-body application pass. Hinge and fixed joints SHALL NOT participate in either pass; they are solved by their own color passes before the iteration's contact accumulation.

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

The per-body application pass SHALL merge the contact partial sums, average them by the contact contribution count (the flag channel), apply them, zero the contact partial cells it consumed, and leave kinematic bodies without delta updates. It SHALL NOT merge or average any hinge or fixed partial sum, because joints no longer produce per-body partials.

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
- **THEN** the body's contact contribution count for that iteration is 2
- **AND** the applied contact delta equals the sum of the two real contributions divided by 2

### Requirement: Explicit accumulator clearing

The solver SHALL clear accumulators with explicit `clear_int_buffer.comp` dispatches rather than relying on the accumulate passes to write zeroes. The correctness of an unwritten entry slot SHALL NOT depend on the entry pass having written a particular key for it. The correctness of an entry slot at or beyond the entry count SHALL NOT depend on its value having been cleared.

The contact per-body partial-sum output buffer SHALL be zeroed once before the substep loop. The contact per-iteration scratch buffer SHALL be zeroed once per position iteration and once per velocity iteration, immediately before its accumulate pass.

A per-iteration scratch clear SHALL be bounded by that substep's entry count rather than by the entry capacity: it SHALL clear the `num_channels` channel planes for slots below the published count, using the entry capacity as the channel stride. Slots at or above the count SHALL NOT need clearing, because the reduction reads values only for entries below the count.

**The clear's count source and its dispatch geometry SHALL follow the contact group's GPU-produced count.** The contact count is produced on the GPU in the same substep, so the counted clear SHALL read it from a buffer inside the shader at execution time, exactly as the radix sort's count guard does. Its number of workgroups SHALL remain derived from the entry capacity — only the number of elements actually written follows the count. This mirrors the level-0 rule of the segmented reduction: the dispatch geometry follows the capacity, the data extent follows the count.

Joint constraints SHALL NOT have a per-iteration scratch buffer or a per-body partial buffer, and therefore SHALL record no counted clear and no partial-sum clear.

The contact position and velocity phases SHALL share a single scratch buffer. This is sound because the phases are strictly sequential — every position iteration completes, then velocities are refreshed from the pose, then the velocity iterations run — and each phase clears the buffer immediately before its accumulate pass, so no position-phase value can reach the velocity reduction.

The clearing of the flat per-body and lagrange buffers SHALL keep using `clear_int_buffer.comp`, whose element count is already a push constant. The counted contact scratch clear SHALL use a separate shader from `clear_int_buffer.comp`, so that no shader declares a binding it does not bind and neither job needs a mode flag.

#### Scenario: A body with no contributions keeps zeros

- **WHEN** a body has no contributing contact in an iteration
- **THEN** its contact partial cells are zero (cleared by the previous apply pass, or by the pre-loop clear on the first iteration)
- **AND** its contribution count is zero and no delta is applied to it

#### Scenario: A non-contributing constraint leaves no residue

- **WHEN** a contact exists this iteration but its penetration is `C <= 0`
- **THEN** the accumulate pass writes nothing for it
- **AND** its entry slot carries zeros with flag 0 from that iteration's scratch clear
- **AND** it contributes to no body's delta sum and to no body's count

#### Scenario: A scratch clear writes only the slots below the count

- **WHEN** a substep's entry count is far below the entry capacity
- **THEN** each scratch clear writes only the slots below that count
- **AND** the number of cleared elements follows the count, not the capacity
- **AND** the number of dispatched workgroups still follows the capacity, because the count is only known on the GPU

#### Scenario: Scratch is cleared per iteration, not per substep

- **WHEN** a substep runs 20 position iterations and 20 velocity iterations
- **THEN** the contact scratch is cleared before each accumulate pass
- **AND** no scratch clear is recorded only once per substep

#### Scenario: Joints record no scratch clear

- **WHEN** a substep runs with hinge and fixed joints present
- **THEN** no per-iteration joint scratch clear is recorded
- **AND** no hinge or fixed per-body partial buffer is cleared

### Requirement: Lagrange multiplier lifetime

Every constraint type's lagrange multiplier buffers SHALL be cleared to zero at the start of each substep, before the position iteration loop, by that type's dedicated clear shader: the contact multipliers by a flat clear, and the hinge and fixed multipliers by `clear_hinge_lagrange.comp` and `clear_fixed_lagrange.comp`, each of which zeroes both of its type's buffers (`HingeAxisLagrange` and `HingeAnchorLagrange`; `FixedRotationLagrange` and `FixedPositionLagrange`).

The contact phase owns three per-contact accumulators, all per contact point, all sized to the contact-point capacity, and all cleared exactly once per substep:

- `ContactLagrange` — the accumulated normal multiplier `lambda_n`, in force-time units (`kg*m` for a length-valued constraint), written by the position phase and read by the velocity phase
- `ContactTangentLagrange` — the accumulated static friction multiplier vector `lambda_t` (`vec4` per contact point, `.xyz` used), same units as `lambda_n`, written and read by the position phase; its clamp is `mu_s * lambda_n`, so it is a substep budget
- `ContactTangentImpulse` — the accumulated kinetic friction impulse vector `p_friction` (`vec4` per contact point, `.xyz` used), in impulse units (`kg*m/s`), written and read by the velocity phase. Unlike the static multiplier it holds the substep's **total** kinetic friction impulse, because the per-iteration impulses telescope into it; its bound `mu_d * |lambda_n| / dt` is enforced by projecting that total into the cone, so it is a substep budget

Because the multipliers are read-modify-written across iterations, a buffer that is not cleared accumulates across substeps without bound: it must not be possible for any of these buffers to be written by an accumulate pass without having been cleared in the same substep. The two tangential accumulators SHALL be cleared once per substep rather than once per iteration of their phase, because their clamps are substep budgets; clearing them per iteration would reinstate the per-iteration re-granting this change removes.

The hinge axis/anchor and fixed rotation/position multipliers SHALL be read-modify-written across a joint's color passes within one substep, so their once-per-substep clear is the same requirement the contact multipliers have: a joint's multiplier that survived into the next substep would make its correction depend on the substep count.

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
- **AND** the joint and color-count buffers remain allocated for the substep's color passes

## REMOVED Requirements

### Requirement: Entry construction is a separate pass from delta accumulation

**Reason**: The requirement covered entry-list construction for every constraint type. Hinge and fixed joints no longer have entry lists, entry passes or published entry counts — they are solved directly from their descriptors through a color bucket index list — so the requirement's joint half describes machinery that no longer exists.

**Migration**: Contact entry construction keeps the same contract under the `Contact entry construction is a separate pass from delta accumulation` requirement added above. The joint behaviour is specified by the `xpbd-joint-color-solve` capability.

### Requirement: Per-substep entry sorting and reuse

**Reason**: The requirement covered per-substep sorting for both the contact group and the hinge/fixed groups. Joints are no longer sorted: their solve order is the color bucket order, which is rebuilt only when the joint graph changes.

**Migration**: Contact sorting and reuse keeps the same contract under the `Per-substep contact entry sorting and reuse` requirement added above. Joint ordering is specified by the `xpbd-joint-color-solve` capability.
