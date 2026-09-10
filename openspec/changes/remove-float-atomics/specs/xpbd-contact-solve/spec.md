# xpbd-contact-solve

## MODIFIED Requirements

### Requirement: Jacobi contact position solving

The system SHALL resolve contact penetration using XPBD in a Jacobi parallelization pattern across per-constraint accumulation passes and a per-body application pass, using scatter + segmented reduction instead of atomic accumulation.

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
11. Scatter the linear and angular position deltas into the solver's sorted-entry scratch buffer at the precomputed permutation position for the contact's entry slot, writing a contribution flag of 1 for affected sides
12. Accumulate `dlambda` into the per-contact lagrange multiplier with a plain `+=` (one thread per contact slot, no contention)
13. After all constraint-type accumulation passes, reduce the scratch buffer to per-body sums and per-body contribution counts via the `SumByKey` segmented reduction

A contact (or contact side) that does not contribute this iteration SHALL write nothing at all; its entry slots carry the zeros left by the per-iteration scratch clear. The per-constraint pass SHALL NOT construct entry keys, SHALL NOT read any mode/phase selector, and SHALL NOT clear accumulators.

The shader SHALL NOT use `SubstepStartPosition` or `SubstepStartOrientation` for contact point coordinate conversion. Local contact points are read directly from the collision detection output and transformed using only current body pose and shape local offset.

The per-body application pass SHALL merge the contact, hinge, and fixed partial sums, average them by the total contribution count (the sum of the flag channel), apply them, zero the per-type partial cells it consumed, and leave kinematic bodies without delta updates.

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

#### Scenario: Skipped contacts do not dilute the average

- **WHEN** a body is touched by three contacts in one iteration
- **AND** one of the three has `C <= 0` and contributes nothing
- **THEN** the body's contribution count for that iteration is 2
- **AND** the applied delta equals the sum of the two real contributions divided by 2

## ADDED Requirements

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

The stored slot ids SHALL remain the identity payload (`entry.y`) so the radix sort permutes them for free.

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

### Requirement: Explicit accumulator clearing

The solver SHALL clear accumulators with explicit `clear_int_buffer.comp` dispatches rather than relying on the accumulate passes to write zeroes. The correctness of an unwritten entry slot SHALL NOT depend on the entry pass having written a particular key for it.

The per-body partial-sum output buffers (one per constraint type) SHALL be zeroed once before the substep loop. Each per-iteration scratch buffer (`num_channels × entry_capacity` elements) SHALL be zeroed once per position iteration and once per velocity iteration, immediately before its accumulate pass.

#### Scenario: A body with no contributions keeps zeros

- **WHEN** a body has no contributing constraint of any type in an iteration
- **THEN** its per-type partial cells are zero (cleared by the previous `apply_*` pass, or by the pre-loop clear on the first iteration)
- **AND** its merged contribution count is zero and no delta is applied to it

#### Scenario: A non-contributing constraint leaves no residue

- **WHEN** a contact exists this iteration but its penetration is `C <= 0`
- **THEN** the accumulate pass writes nothing for it
- **AND** its entry slot carries zeros with flag 0 from that iteration's scratch clear
- **AND** it contributes to no body's delta sum and to no body's count

#### Scenario: Scratch is cleared per iteration, not per substep

- **WHEN** a substep runs 20 position iterations and 20 velocity iterations
- **THEN** each active constraint type records a scratch clear before each of its accumulate passes
- **AND** no scratch clear is recorded only once per substep

### Requirement: Per-substep entry sorting and permutation reuse

Per substep, after collision detection completes, the system SHALL build the contact entry list once (each contact side contributing a `(body, slot)` pair, with unowned slots marked `INVALID`), sort it by body via the radix sort's primary-key mode, and invert the resulting permutation into a `pos_of` map plus the sorted key array.

All position-solve iterations and all velocity-solve iterations of that substep SHALL reuse the same permutation: accumulation passes SHALL write their per-iteration values through `pos_of` into per-channel scratch arrays, and the per-body reduction SHALL consume the sorted keys. Re-sorting SHALL NOT occur per iteration.

The hinge and fixed entry lists SHALL be rebuilt, sorted and inverted once per substep as well, with no entry list cached across substeps. Buffer objects and the `RadixSort`/`SumByKey` instances MAY be re-created only when a joint or body count changes, because their capacity is a construction-time parameter.

#### Scenario: Sort runs once per substep, not per iteration

- **WHEN** a substep runs 20 position iterations and 20 velocity iterations
- **THEN** the contact entry list is sorted exactly once during that substep
- **AND** every iteration's accumulation writes through the same `pos_of` permutation

#### Scenario: Velocity iterations reuse the position permutation

- **WHEN** the velocity-solve phase of a substep begins
- **THEN** no new sort or permutation inversion is recorded for the contact list
- **AND** velocity accumulation scatters into the same sorted layout as position accumulation

#### Scenario: Joint entry lists are rebuilt every substep

- **WHEN** the hinge joint and body counts are unchanged between two substeps
- **THEN** the hinge entry list is still rebuilt, sorted and inverted in the second substep
- **AND** no entry list is carried over from the first substep
