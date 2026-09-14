# xpbd-contact-solve

## MODIFIED Requirements

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
