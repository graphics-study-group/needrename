## MODIFIED Requirements

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
