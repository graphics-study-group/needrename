## MODIFIED Requirements

### Requirement: Joint colors are published as a bucket index list

The scene SHALL keep independent fixed and hinge CPU color buckets, but SHALL publish their GPU metadata as one shared `JointColorIds` array and one shared `JointColorOffsets` array. The IDs array SHALL contain `fixed.ids` followed by `hinge.ids`; the offsets array SHALL contain `fixed.offsets` followed by `hinge.offsets`. Both tables SHALL be rebuilt and fully re-uploaded from newly concatenated CPU data whenever joint colors are rebuilt, and SHALL NOT be rebuilt on a frame in which no coloring input changed.

Within each joint type, a color's joints SHALL occupy a contiguous slice and each alive joint SHALL appear exactly once. Joint descriptor arrays SHALL NOT be reordered, and within a color the index array SHALL preserve joint slot order.

If `Cf` is the fixed color count, the hinge offsets base SHALL be `Cf + 1`, and the hinge IDs base SHALL be `JointColorOffsets[Cf]`, which equals the number of alive fixed joints rather than the fixed descriptor capacity. Fixed color indices remain local to the start of the shared arrays. Hinge sweep accesses SHALL add these derived bases, and per-color hinge dispatches SHALL carry `JointColorOffsets[Cf] + hinge_ranges[color]` as their global color offset.

Both dispatch modes SHALL bind the same full shared IDs array with descriptor offset zero. Empty types SHALL retain valid table allocations without adding a second set of per-type GPU buffers.

#### Scenario: Every alive joint appears exactly once

- **WHEN** fixed and hinge color tables are rebuilt
- **THEN** the shared IDs array contains every alive fixed joint exactly once followed by every alive hinge joint exactly once
- **AND** the shared offsets array contains both independent per-color range tables in the same order

#### Scenario: A joint's slot id is stable across a rebuild

- **WHEN** the shared color tables change because a joint was added or disabled
- **THEN** every remaining joint keeps its slot id in the scene's joint arrays
- **AND** only its index in the shared color IDs table can change

#### Scenario: Disabled joints are excluded

- **WHEN** a joint is disabled and the shared tables are rebuilt
- **THEN** the joint's slot id does not appear in the shared IDs array
- **AND** neither dispatch mode can reach it

#### Scenario: Hinge bases account for alive fixed joints

- **WHEN** the fixed descriptor set contains dead slots and both joint types are present
- **THEN** the hinge IDs base is the fixed offsets table's final value, not the fixed descriptor capacity
- **AND** every hinge color resolves to its own joint slots in the shared IDs array

#### Scenario: Per-color fallback uses shared IDs

- **WHEN** a large color disables the single-workgroup sweep
- **THEN** fixed and hinge per-color dispatches bind the same shared IDs array and use their appropriate global element offsets
- **AND** their color order and joint evaluations match the previous per-type-table behavior

#### Scenario: Unchanged topology does not rebuild

- **WHEN** a frame changes no joint's alive flag, body ownership or body kinematic flag
- **THEN** the color assignment and both shared tables are reused rather than recomputed or re-uploaded

### Requirement: A single workgroup may sweep all colors in one dispatch

When every fixed and hinge color fits in one workgroup under the existing sweep-width policy, the solver MAY record the whole joint sweep as a single dispatch whose only workgroup walks fixed colors and then hinge colors in their established order. Between two colors the shader SHALL make its body writes visible to the following color with workgroup-scope storage synchronization followed by workgroup execution synchronization.

The sweep SHALL bind no more than 16 storage buffers, including the two shared color metadata buffers, so its pipeline can be created under the browser's baseline limit. The implementation SHALL NOT request a higher storage-buffer limit to enable this mode.

The shader SHALL NOT return, discard or otherwise leave the workgroup before the final barrier: every invocation SHALL reach every barrier in the same order. A joint that cannot contribute SHALL skip its work without leaving the barrier sequence.

The two modes SHALL produce equivalent results for the same scene within ordinary floating-point tolerance: the mode is a dispatch-count decision, not a change in solve semantics, substep count, iteration count, or constraint order.

#### Scenario: A small joint set is solved in one dispatch

- **WHEN** every fixed and hinge color satisfies the configured single-workgroup sweep-width limit
- **THEN** the joint sweep is one dispatch
- **AND** it writes equivalent body poses to one dispatch per color within the solver's numerical tolerance

#### Scenario: A color larger than one workgroup uses per-color dispatches

- **WHEN** any color exceeds the configured single-workgroup sweep-width limit
- **THEN** the joint sweep is recorded as one dispatch per color
- **AND** the single-workgroup mode is not used

#### Scenario: A buffer write is visible to the next color

- **WHEN** a color writes a body pose that a later color reads
- **THEN** the write is ordered before the read by workgroup-scope storage and execution synchronization
- **AND** the later color observes the corrected pose, not the pose from the start of the dispatch

#### Scenario: Browser binding budget is respected

- **WHEN** the sweep pipeline is created for the browser backend
- **THEN** it declares at most 16 storage buffers and requires no elevated device limit

#### Scenario: A barrier reached by some invocations only is a violation

- **WHEN** a joint's work is skipped by an early return rather than by a guarded work block
- **THEN** the workgroup can deadlock or read stale poses
- **AND** the implementation is non-conforming
