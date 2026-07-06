# global-pair-generation-pass

## Purpose

Govern the dedicated GPU compute pass that generates collision pairs involving global shapes (shapes spanning too many cells or outside world bounds). Global shapes are indexed into a compact list during AABB computation and consumed by a 2D-dispatched shader that pairs each global shape against all alive shapes.

## Requirements

### Requirement: Compact global-shape index list

During AABB computation, the `compute_aabbs.comp` shader SHALL write each global shape's index into a compact `global_list[]` buffer via atomic append. A shape is global if and only if its AABB spans more than `max_cells_per_shape` cells. Shapes entirely outside `[grid_world_min, grid_world_max]` SHALL NOT be marked global.

A separate `global_count[]` buffer SHALL track the total number of global shapes. The `global_list[]` buffer SHALL be sized to `shape_slot_count` (worst case: all shapes are global). The `global_count[]` buffer SHALL be cleared to zero before the AABB pass each frame.

#### Scenario: Global shape appended to list

- **WHEN** `compute_aabbs.comp` marks `global_flags[i] = 1` for shape `i` because `num_cells > max_cells_per_shape`
- **THEN** shape index `i` is written to `global_list[atomicAdd(global_count[0], 1)]`

#### Scenario: No global shapes produces empty list

- **WHEN** all shapes have AABBs spanning ≤ `max_cells_per_shape` cells
- **THEN** `global_count[0] = 0` after AABB pass
- **AND** no entries are written to `global_list[]`

#### Scenario: Out-of-bounds shape not in global list

- **WHEN** a shape is entirely outside world bounds
- **THEN** its index is NOT written to `global_list[]`
- **AND** `global_count` is NOT incremented for this shape

### Requirement: Dedicated global pair generation shader

The detector SHALL provide a dedicated compute shader (`generate_global_pairs.comp`) that generates all collision pairs involving global shapes. The shader SHALL NOT enumerate all possible shape pairs — instead it SHALL iterate only over `global_global_count × shape_slot_count` threads via a 2D dispatch.

Dispatch dimensions SHALL be `X = ceil(shape_slot_count / 64)`, `Y = global_count`, `Z = 1`, with local size `(64, 1, 1)`.

Each thread SHALL map `gl_WorkGroupID.y` to a global shape index `g = global_list[g_idx]` and `gl_GlobalInvocationID.x` to a target shape index `s`. The thread SHALL generate pair `(min(g,s), max(g,s))` if all of the following hold:
- `s != g` (no self-collision)
- `shape_alive[s] != 0` (target is alive)
- NOT `(global_flags[s] != 0 AND g > s)` (dedup: only the smaller global index emits global×global pairs)
- The global shape `g`'s AABB and the target shape `s`'s AABB overlap (3-axis separating-axis check on `aabb_min`/`aabb_max`)
- The pair is not in the collision filter

The shader SHALL bind `AabbMin` and `AabbMax` as `readonly buffer`. The C++ pass SHALL declare both reads via `UseBuffer(..., RR)` so the render graph inserts the read-after-write barrier from `compute_aabbs`. The AABB overlap test SHALL be evaluated after the `shape_alive` check and before the collision-filter binary search.

#### Scenario: Global shape paired with non-global shape

- **WHEN** global shape 5 and non-global shape 3 are both alive
- **AND** their world-space AABBs overlap
- **THEN** thread (s=3, g_idx_of_5) emits pair (3, 5)

#### Scenario: Two global shapes deduplicated

- **WHEN** global shapes 5 and 8 are both alive (both span too many cells)
- **AND** their world-space AABBs overlap
- **THEN** thread (s=8, g_idx_of_5) emits pair (5, 8) because `5 < 8`
- **AND** thread (s=5, g_idx_of_8) skips because `global_flags[5] != 0 AND 8 > 5`

#### Scenario: Zero global shapes produces no pairs

- **WHEN** `global_count = 0`
- **THEN** Y dispatch dimension is 0
- **AND** the shader produces no output

#### Scenario: All shapes are global

- **WHEN** all N shapes are marked global (all span > max_cells_per_shape)
- **AND** every pair of shapes has overlapping AABBs
- **THEN** the shader generates exactly N×(N−1)/2 pairs (same as all-pairs)
- **AND** each pair appears exactly once due to dedup logic

#### Scenario: Global shape with non-overlapping target AABB pruned

- **WHEN** global shape 5 (large AABB) is paired with alive shape 20
- **AND** shape 20's AABB is entirely outside shape 5's AABB on at least one axis
- **THEN** thread (s=20, g_idx_of_5) skips emission
- **AND** the pair (5, 20) is not written to the output pair buffer

### Requirement: Global pair pass integration

The global pair generation pass SHALL be dispatched after the within-cell pair generation pass and before returning the output buffers to the caller. Pairs from the global pass SHALL be appended to the same `collision_pairs[]` output buffer using the same atomic `pair_count` counter.

A clear of `PairCount` SHALL precede the within-cell generation; within-cell pairs and global pairs SHALL share a single atomic counter, producing a single contiguous output buffer.

#### Scenario: Pairs appended to shared output

- **WHEN** within-cell generation produces 100 pairs (written at indices 0–99, `pair_count = 100`)
- **AND** global pass generates 15 pairs
- **THEN** global pairs are written at indices 100–114
- **AND** final `pair_count = 115`

### Requirement: Global count cleared per frame

The `global_count` buffer SHALL be cleared to zero before each frame's AABB pass. This SHALL use the existing `memset_uint` shader with the constant-one element count buffer.

#### Scenario: Global count reset between frames

- **WHEN** frame N produces 3 global shapes
- **AND** the clear pass runs before frame N+1's AABB pass
- **THEN** `global_count[0] = 0` at the start of AABB computation for frame N+1
