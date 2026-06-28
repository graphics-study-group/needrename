# global-pair-generation-pass

Delta spec for the global pair generation pass. Change: shapes outside world bounds are no longer marked global — only shapes spanning too many cells trigger global pair generation.

## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: Shape outside bounds marked global

**Reason**: Out-of-bounds shapes should not interact with any in-bounds shapes. Marking them global generated N−1 wasteful pairs per out-of-bounds shape. The new behavior silently ignores them.

**Migration**: Out-of-bounds shapes are now excluded from all collision pair generation. No migration needed — this is a behavior change with no API impact.
