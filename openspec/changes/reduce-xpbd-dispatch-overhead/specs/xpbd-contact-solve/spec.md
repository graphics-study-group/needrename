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

Before evaluating a contact, each accumulate invocation SHALL initialize all channels of both entry slots owned by that contact to zero. This initialization SHALL occur before any contact-specific guard can return, so a contact or contact side that does not contribute leaves zero deltas and a zero contribution flag. A contributing side SHALL overwrite its initialized slot through the existing scatter operation. Initialization SHALL NOT clear the per-contact lagrange or friction multipliers.

The per-constraint pass SHALL NOT construct entry keys, SHALL NOT look up a permutation map, SHALL NOT read any mode/phase selector, and SHALL NOT depend on a separate scratch-clear dispatch. Slots at or beyond the published entry count need no initialization because the reduction does not read them.

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
- **THEN** the skipped contact's initialized flag remains zero
- **AND** the body's contact contribution count for that iteration is 2
- **AND** the applied contact delta equals the sum of the two real contributions divided by 2

### Requirement: Contact entry construction is a separate pass from delta accumulation

Contact entry-list construction and per-iteration delta accumulation SHALL be separate compute shaders, not two modes of one shader selected by a push constant. The contact accumulate shader SHALL NOT carry a mode/phase selector in its push-constant block, and SHALL NOT write an entry-pair buffer.

The contact entry pass SHALL be dispatched over the contact entry **capacity** — one invocation per contact point, writing 2 slots — and SHALL write every slot in `[0, capacity)` on every dispatch. It SHALL NOT return before writing a slot it owns; because the workgroup count rounds up, an invocation whose last slot index is `>= capacity` owns no slots and MAY return immediately (the bound travels in the push-constant block). A slot whose contact does not exist this substep, or whose owning body index is out of range, SHALL be written with the `INVALID` key (`0xFFFFF`).

The contact entry pass SHALL derive keys from slot **ownership** only and SHALL NOT read body state: it SHALL NOT read rigid body alive flags, kinematic flags or masses. Every decision about whether a contact contributes SHALL live in the contact accumulate shader, which SHALL retain its guard structure (alive-body checks, kinematic checks, inverse-mass checks, `C > 0` and `denom > ε` gates) after slot initialization.

Hinge and fixed joints SHALL have no entry list, no entry pass and no published entry count: they are solved by the joint color passes, which read the joint descriptors through their color bucket list.

The stored slot ids SHALL remain the identity payload (`entry.y`) so the radix sort permutes them for free, and SHALL be the index the segmented reduction reads each entry's values from at level 0.

The contact entry pass SHALL additionally establish the substep's **entry count** — the number of leading slots that can carry an owner this substep — and SHALL publish it to the count buffer that the radix sort and the segmented reduction both read at execution time. The published count SHALL be clamped so that no slot at or above the capacity is included, and entries at or beyond the published count SHALL be treated as absent by both the sort and the reduction: the sort skips them, and the segmented reduction SHALL NOT read their values from the per-iteration scratch.

The contact count SHALL be derived on the GPU from the collision count (2 slots per contact point), because the collision count is produced on the GPU in the same substep: `min(2 * collision_count, entry_capacity)`. The entry pass SHALL write this value into the count buffer itself; the host SHALL NOT write the contact count. The write order SHALL NOT matter beyond the barrier that already separates entry construction from the sort.

A slot's key choice is what makes "treated as absent" sound. The pair array's keys are sorted, so the reduction can only recognise a data extent that is also a prefix of the key order: everything at or beyond the count must sort **after** every entry that can contribute, which is what makes "ignore everything from the count on" the same statement as "ignore every entry whose key is at or above `max_key_value`". Contacts get that for free, because the entry pass writes `INVALID` (`0xFFFFF`) for every slot whose contact does not exist.

The values of slots at or beyond the count are **not** required to be initialized. The count is the reduction's only read bound, so a slot at or beyond it is never read. Each accumulate invocation initializes only slots belonging to a contact below the published collision count and contact-point capacity.

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

### Requirement: Explicit accumulator clearing

The solver SHALL clear flat per-body and lagrange accumulators with explicit `clear_int_buffer.comp` or type-specific clear dispatches. The contact per-body partial-sum output buffer SHALL be zeroed once before the substep loop and by the apply pass as it consumes partials. Joint constraints SHALL NOT have a per-iteration scratch buffer or per-body partial buffer and SHALL record no scratch or partial-sum clear.

The contact per-iteration scratch buffer SHALL be initialized by the position and velocity accumulate passes rather than by a standalone clear shader. Every invocation that owns a contact below the published collision count and contact-point capacity SHALL zero all channels of that contact's two slots before any contact-specific early return. The existing scatter operation SHALL overwrite those zeros only for a side that receives a contribution.

The contact position and velocity phases SHALL share one scratch buffer. Their strict sequence and per-invocation initialization prevent position values from reaching the velocity reduction. The standalone counted entry-value clear shader and its dispatches SHALL be removed; no replacement multi-buffer clear or mode flag SHALL be introduced.

#### Scenario: A body with no contributions keeps zeros

- **WHEN** a body has no contributing contact in an iteration
- **THEN** its contact partial cells are zero, cleared by the previous apply pass or by the pre-loop clear on the first iteration
- **AND** its contribution count is zero and no delta is applied to it

#### Scenario: A non-contributing constraint leaves no residue

- **WHEN** a contact exists this iteration but its penetration is `C <= 0`
- **THEN** its accumulate invocation has already initialized both slots
- **AND** its entry slots carry zeros with flag 0
- **AND** it contributes to no body's delta sum or count

#### Scenario: Every early-return path leaves initialized slots

- **WHEN** an accumulate invocation exits because of an invalid shape or body, dead or kinematic bodies, zero inverse mass, an invalid normal, or a zero impulse
- **THEN** both of its entry slots retain the zeros written before that guard
- **AND** no stale value from an earlier iteration reaches the reduction

#### Scenario: A scratch clear writes only the slots below the count

- **WHEN** a substep's entry count is below the entry capacity
- **THEN** accumulate invocations initialize slots belonging to their live contacts
- **AND** slots beyond the published count remain unread and need no writes

#### Scenario: Scratch is cleared per iteration, not per substep

- **WHEN** a substep runs multiple position and velocity iterations
- **THEN** each accumulate pass initializes its own slots on every iteration and no `ClearEntryValues` dispatch is recorded between them

#### Scenario: Joints record no scratch clear

- **WHEN** a substep runs with hinge and fixed joints present
- **THEN** no per-iteration joint scratch clear is recorded
- **AND** no hinge or fixed per-body partial buffer is cleared
