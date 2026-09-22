# xpbd-contact-solve

## MODIFIED Requirements

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
