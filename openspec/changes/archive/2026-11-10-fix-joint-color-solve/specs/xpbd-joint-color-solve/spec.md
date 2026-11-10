## Purpose

Defines how hinge and fixed joints are solved on the GPU: joint colors that guarantee a color's joints share no body, the color-bucketed index list the dispatches consume, the direct pose write that replaces the contact accumulation pipeline for joints, and the two dispatch modes that sweep the colors.

## ADDED Requirements

### Requirement: Joint constraints are solved by a colored sweep of direct pose writes

Each position iteration SHALL solve hinge and fixed joints as a sequence of color passes that run before the iteration's contact accumulation. In one color pass, every joint of that color SHALL read both of its bodies' current poses, evaluate its constraints, and write the corrected position and orientation of both bodies directly. The joint path SHALL NOT use a per-body partial-sum buffer, a contribution count, an entry-slot scratch buffer, a segmented reduction, or a per-body application pass.

Because two joints of the same color never share a body (see the color-partition requirement), no two invocations of a color pass target the same body. Direct writes therefore require no atomics, no accumulation and no averaging.

A joint's correction SHALL be applied at full strength: it SHALL NOT be divided by the number of constraints touching either body. The two bodies of a joint SHALL receive equal-and-opposite linear corrections scaled by their inverse masses, so a joint conserves momentum, and a body marked kinematic SHALL receive no correction at all.

A joint's lagrange multipliers SHALL remain per-joint and SHALL be read-modify-written across the iteration's colors, cleared once per substep before the position iteration loop.

#### Scenario: A joint is not diluted by contacts on the same body

- **WHEN** a dynamic body carries one hinge joint and several contact slots in the same position iteration
- **THEN** the correction the hinge applies to that body is the joint's full change, not that change divided by the number of constraints touching the body
- **AND** the body on the joint's other side receives the matching opposite correction

#### Scenario: A chain body receives every incident joint's full correction

- **WHEN** a chain of three bodies is connected by two joints and the two joints are assigned different colors
- **THEN** the middle body receives the first joint's full correction in the first color pass and the second joint's full correction in the second
- **AND** neither correction is scaled by the fact that two joints touch that body

#### Scenario: A joint to a kinematic body moves only the dynamic side

- **WHEN** a hinge or fixed joint connects a dynamic body to a kinematic body
- **THEN** the dynamic body receives the full correction
- **AND** the kinematic body's pose and velocity are unchanged

#### Scenario: A disabled joint contributes nothing

- **WHEN** a joint's alive flag is zero for the substep
- **THEN** the joint is not present in any color pass
- **AND** neither of its bodies receives a correction from it

### Requirement: Joint colors partition joints so that a color's joints share no body

The system SHALL assign each joint a color such that two joints sharing at least one body never share a color, using deterministic greedy first-fit over the joints' slot order. Hinge joints and fixed joints SHALL be colored as independent graphs.

The color assignment SHALL be derived from the alive joint set: a joint with a zero alive flag SHALL consume no color, and a joint whose body index is out of range SHALL consume no color. A body marked kinematic SHALL NOT create a color conflict, because the solver never writes it.

The number of colors SHALL be published with the scene's GPU buffer set for each joint type, and SHALL be zero when that type has no alive joint.

#### Scenario: Four hip joints on one trunk force four hinge colors

- **WHEN** a quadruped has four hinge joints that all attach a leg to the same trunk body, and its remaining hinge joints form three-bone chains
- **THEN** the color count is at least four, because the four hip joints pairwise share the trunk
- **AND** no two of those four hip joints are in the same color

#### Scenario: A kinematic base does not consume colors

- **WHEN** several hinge joints attach different bodies to the same kinematic body
- **THEN** those joints may share one color
- **AND** the color count does not grow with the number of joints attached to the kinematic body

#### Scenario: A disabled joint consumes no color

- **WHEN** a joint that previously forced an extra color is disabled and the joint graph is rebuilt
- **THEN** its color is no longer reserved
- **AND** the published color count reflects only the alive joints

#### Scenario: Coloring is deterministic

- **WHEN** the same joint set, slot order and kinematic flags are colored twice
- **THEN** both runs produce the same color for every joint and the same color count

### Requirement: Joint colors are published as a bucket index list

For each joint type the scene SHALL publish an index array ordered by color, together with a per-color range of that array. A color's joints SHALL occupy a contiguous slice of the array, and each alive joint SHALL appear in the array exactly once.

The joint descriptor arrays SHALL NOT be reordered: the index array is the only indirection between a color's slice position and a joint's slot. Within a color the array SHALL preserve the joints' slot order, so that a color's slice keeps whatever locality the slot order already has.

The index array and range table SHALL be rebuilt when the alive joint set, a joint's body ownership, or a body's kinematic flag changes, and SHALL NOT be rebuilt on a frame in which none of those changed.

#### Scenario: A joint's slot id is stable across a rebuild

- **WHEN** the color assignment changes because a joint was added or disabled
- **THEN** every remaining joint keeps its slot id in the scene's joint arrays
- **AND** the constraint component that owns a joint still addresses the same slot

#### Scenario: Every alive joint appears exactly once

- **WHEN** the index array is rebuilt for a joint type
- **THEN** its length equals the number of alive joints of that type
- **AND** each of those joints' slot ids appears exactly once

#### Scenario: Disabled joints are excluded

- **WHEN** a joint is disabled and the index array is rebuilt
- **THEN** the joint's slot id does not appear in the array
- **AND** no color pass can reach it

#### Scenario: Unchanged topology does not rebuild

- **WHEN** a frame changes no joint's alive flag, body ownership or body kinematic flag
- **THEN** the color assignment and the index array are reused rather than recomputed

### Requirement: Each color dispatch covers only that color's joints

The solver SHALL record one dispatch per non-empty color, with a workgroup count of `ceil(n_c / workgroup_size)` where `n_c` is that color's joint count, and SHALL record no dispatch for an empty color. A color pass SHALL be separated from the next by a barrier that makes its body writes visible to the following pass.

#### Scenario: A color with five joints dispatches one workgroup

- **WHEN** a color owns five joints and the workgroup size is at least five
- **THEN** that color is recorded as a single workgroup
- **AND** the invocations beyond the color's joint count do no work

#### Scenario: Dispatch count follows each color's joint count

- **WHEN** a color owns more joints than one workgroup covers
- **THEN** that color's dispatch covers every joint of the color with no invocation outside the color's range

#### Scenario: Every joint is solved exactly once per iteration

- **WHEN** a position iteration runs
- **THEN** each alive joint is processed by exactly one color pass
- **AND** the number of joint evaluations equals the number of alive joints

#### Scenario: An empty joint type records nothing

- **WHEN** a scene has no hinge joints
- **THEN** no hinge color pass is recorded
- **AND** the position iteration still records its contact and fixed-joint passes

### Requirement: A single workgroup may sweep all colors in one dispatch

When every color of a joint type fits in one workgroup, the solver MAY record the whole joint sweep as a single dispatch whose only workgroup walks the colors in order. Between two colors the shader SHALL make its body writes visible to the following pass with a buffer memory barrier followed by a workgroup execution barrier.

The shader SHALL NOT return, discard or otherwise leave the workgroup before the final barrier: every invocation SHALL reach every barrier in the same order. A joint that cannot contribute SHALL skip its work without leaving the barrier sequence.

The two modes SHALL produce the same result for the same scene: the mode is a dispatch-count decision, not a change in solve semantics.

#### Scenario: A small joint set is solved in one dispatch

- **WHEN** a scene's largest color holds no more joints than one workgroup covers
- **THEN** the joint sweep is one dispatch
- **AND** it writes the same body poses as one dispatch per color would

#### Scenario: A color larger than one workgroup uses per-color dispatches

- **WHEN** a scene's largest color holds more joints than one workgroup covers
- **THEN** the joint sweep is recorded as one dispatch per color
- **AND** the single-workgroup mode is not used

#### Scenario: A buffer write is visible to the next color

- **WHEN** a color writes a body pose that a later color reads
- **THEN** the write is ordered before the read by the shader's buffer memory barrier and workgroup barrier
- **AND** the later color observes the corrected pose, not the pose from the start of the dispatch

#### Scenario: A barrier reached by some invocations only is a violation

- **WHEN** a joint's work is skipped by an early return rather than by a guarded work block
- **THEN** the workgroup can deadlock or read stale poses
- **AND** the implementation is non-conforming
