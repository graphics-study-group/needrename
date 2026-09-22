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
11. Scatter the linear and angular position deltas into the solver's sorted-entry scratch buffer at the contact's own entry slot index (`scratch[c * entry_capacity + slot]`), writing a contribution flag of 1 for affected sides
12. Accumulate `dlambda` into the per-contact lagrange multiplier with a plain `+=` (one thread per contact slot, no contention)
13. After all constraint-type accumulation passes, reduce the scratch buffer to per-body sums and per-body contribution counts via the `SumByKey` segmented reduction, which reads the sorted `(key, slot)` pair array at level 0 and gathers each entry's values by the slot that pair carries

A contact (or contact side) that does not contribute this iteration SHALL write nothing at all; its entry slots carry the zeros left by the per-iteration scratch clear. The per-constraint pass SHALL NOT construct entry keys, SHALL NOT look up a permutation map, SHALL NOT read any mode/phase selector, and SHALL NOT clear accumulators.

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

The counted clear SHALL read the entry count from a buffer inside the shader at execution time, exactly as the radix sort's count guard does, because the count is produced on the GPU in the same substep. The number of workgroups it dispatches SHALL therefore still be derived from the entry capacity — only the number of elements actually written SHALL follow the count. This mirrors the level-0 rule of the segmented reduction: the dispatch geometry follows the capacity, the data extent follows the count.

The contact position and velocity phases SHALL share a single scratch buffer. This is sound because the phases are strictly sequential — every position iteration completes, then velocities are refreshed from the pose, then the velocity iterations run — and each phase clears the buffer immediately before its accumulate pass, so no position-phase value can reach the velocity reduction.

The clearing of the flat per-body and lagrange buffers SHALL keep using `clear_int_buffer.comp`; the counted scratch clears SHALL use a separate shader that reads the entry count from a buffer, so neither job needs a mode flag.

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
- **AND** the number of dispatched workgroups still follows the capacity, because the count is only known on the GPU

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

Every constraint type's lagrange multiplier buffers SHALL be cleared to zero at the start of each substep, before the position iteration loop, by that type's dedicated clear shader: the contact multiplier by `clear_int_buffer.comp` (or an equivalent float clear), and the hinge and fixed multipliers by `clear_hinge_lagrange.comp` and `clear_fixed_lagrange.comp`, each of which zeroes both of its type's buffers (`HingeAxisLagrange` and `HingeAnchorLagrange`; `FixedRotationLagrange` and `FixedPositionLagrange`). These buffers hold floats and SHALL be cleared by their own shaders rather than by the integer-clear workaround used for the integer accumulators.

Within a substep, each position-solve iteration SHALL accumulate `dlambda` into the lagrange multiplier. The velocity-solve phase SHALL read the accumulated contact lagrange multiplier to compute the friction impulse cap.

Because the multipliers are read-modify-written across iterations, a buffer that is not cleared accumulates across substeps without bound: it must not be possible for any multiplier buffer to be written by an accumulate pass without having been cleared in the same substep.

#### Scenario: Every multiplier buffer is cleared each substep

- **WHEN** a substep runs with contacts, hinge joints and fixed joints present
- **THEN** the contact, hinge-axis, hinge-anchor, fixed-rotation and fixed-position multiplier buffers each receive exactly one clear dispatch before the position iterations
- **AND** each clear covers that type's own element count

#### Scenario: No multiplier is accumulated across substeps

- **WHEN** the same constraint is solved in two consecutive substeps with identical geometry
- **THEN** the second substep's first iteration reads a multiplier of zero, not the previous substep's accumulated value

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
