# spatial-hash-broad-phase

Delta spec for the spatial-hash broad-phase detector. Changes: out-of-bounds shapes are silently ignored instead of marked global; dedup pass added after pair generation.

## MODIFIED Requirements

### Requirement: Global shape handling

Shapes whose AABB spans more than `max_cells_per_shape` cells SHALL be marked as global (`global_flags[i] = 1`). Shapes whose AABB is entirely outside `[grid_world_min, grid_world_max]` SHALL NOT be marked global — they SHALL be silently ignored (no pairs generated, not included in `global_list[]`).

During AABB computation, global shape indices (spanning too many cells) SHALL be written into a compact `global_list[]` buffer via atomic append (`global_count` tracks the count). A separate `global_count` clear pass (using `memset_uint.comp`) SHALL run before each frame's AABB pass.

After within-cell pair generation, a dedicated `generate_global_pairs.comp` shader SHALL generate pairs between each global shape and every other alive shape (both global and non-global) using a 2D dispatch `(ceil(N/64), G, 1)` where G is the number of global shapes. Global×global pairs SHALL be deduplicated by only emitting from the smaller global index.

#### Scenario: Large shape marked global

- **WHEN** a shape's AABB spans 27 cells and `max_cells_per_shape = 8`
- **THEN** `global_flags[i] = 1`
- **AND** no cell-assignment entries are generated for this shape
- **AND** shape `i` is appended to `global_list[]`
- **AND** `generate_global_pairs.comp` generates pairs `(min(i,j), max(i,j))` for all `j != i`

#### Scenario: Shape outside bounds is silently ignored

- **WHEN** a shape's AABB is entirely outside `[grid_world_min, grid_world_max]`
- **THEN** `global_flags[i] = 0`
- **AND** the shape is NOT appended to `global_list[]`
- **AND** `count_cells.comp` and `fill_cells.comp` skip this shape (explicit out-of-bounds check)
- **AND** no collision pairs involving this shape are generated

#### Scenario: Shape partially outside bounds is not ignored

- **WHEN** a shape's AABB overlaps the grid boundary but is not entirely outside
- **THEN** the AABB SHALL be clamped to grid bounds for cell-range computation
- **AND** the shape participates normally in cell assignment and pair generation
- **AND** `global_flags[i]` is set only if `num_cells > max_cells_per_shape`

### Requirement: Within-cell collision pair generation

The detector SHALL generate candidate collision pairs from the sorted cell-shape data in a compute shader (`generate_broad_pairs.comp`). For each cell `c`, all alive shapes assigned to that cell SHALL be paired in upper-triangle order (all pairs `(i, j)` with `i < j` within the cell's shape list).

The shader SHALL iterate over cells using `cell_offsets[]` to determine the range of shapes in each cell. Each workgroup processes multiple cells (one thread per cell). Global shapes and out-of-bounds shapes are not included in this shader — global shapes are handled by `generate_global_pairs.comp`; out-of-bounds shapes are excluded during cell assignment.

Each generated pair SHALL satisfy `index_a < index_b`. Dead shapes SHALL be excluded from pair generation (checked via `shape_alive`).

**Note**: Within-cell pair generation MAY produce duplicate pairs when two shapes share multiple cells. These duplicates SHALL be removed by the post-generation dedup step.

#### Scenario: Cell with three shapes produces three pairs

- **WHEN** cell 0 contains shapes with indices {2, 5, 7}
- **THEN** the shader generates pairs (2, 5), (2, 7), (5, 7) for that cell

#### Scenario: Cell with one shape produces no pairs

- **WHEN** cell 0 contains only shape index 3
- **THEN** no pairs are generated for cell 0

#### Scenario: Dead shape excluded

- **WHEN** a shape in cell 0 is marked dead in `shape_alive`
- **THEN** no pair involving that shape is generated
- **AND** the cell's effective population for pair generation is reduced by 1

## ADDED Requirements

### Requirement: Post-generation pair deduplication

After within-cell pair generation (`generate_broad_pairs.comp`) and global pair generation (`generate_global_pairs.comp`) complete, the detector SHALL perform deduplication on the combined `collision_pairs[]` output buffer before returning results to the caller.

The dedup SHALL proceed in two stages:
1. **Sort**: `RadixSort::AddPasses` sorts all pairs in `collision_pairs[]` by `(a, b)` ascending. The sorted result SHALL overwrite `collision_pairs[]`.
2. **Compact**: `CompactUnique::AddPasses` removes adjacent duplicates and compacts the result. The compacted unique pairs SHALL overwrite `collision_pairs[]` and `pair_count` SHALL be updated via a copy from `unique_count`.

**Dispatch sizing**: Both stages SHALL pass `max_pairs` (buffer capacity) as `elem_capacity` for dispatch workgroup sizing. The actual pair count (`gpu_pair_count`) SHALL be passed as a GPU buffer binding — each shader reads this at execution time to skip threads beyond the valid range. At RenderGraph build time, `gpu_pair_count` is NOT read via `GetVMAddress()` (which would return stale data since the GPU hasn't executed yet).

The detector SHALL allocate the following additional buffers:
- `gpu_pairs_temp`: ping-pong temp for RadixSort (`max_pairs × sizeof(glm::uvec2)`)
- `gpu_radix_scratch`: 256-uint histogram (1 KB)
- `gpu_unique_flags`: original 0/1 flags (`max_pairs × sizeof(uint32_t)`)
- `gpu_unique_offsets`: prefix-sum offsets, same size as flags
- `gpu_unique_count`: output unique count (`sizeof(uint32_t)`, host-visible)

The detector SHALL use its existing `ParallelScan` instance for `CompactUnique`'s internal prefix sum.

The dedup SHALL be skipped if the fallback all-pairs path is used (fallback already produces unique pairs by construction).

#### Scenario: Duplicate pairs are removed

- **WHEN** within-cell generation produces pairs `[(1,4), (2,3), (1,4)]` (pair (1,4) appears twice)
- **AND** no global pairs are generated
- **THEN** after dedup, `collision_pairs[]` contains `[(1,4), (2,3)]` (or `[(2,3), (1,4)]` — order after sort)
- **AND** `pair_count = 2`

#### Scenario: Within-cell and global pairs are deduplicated together

- **WHEN** within-cell generation produces pair (3, 7)
- **AND** global pair generation also produces pair (3, 7) because shape 3 is global
- **THEN** after dedup, pair (3, 7) appears exactly once in the output

#### Scenario: Dedup skipped for fallback path

- **WHEN** the fallback all-pairs path is used (shape_count ≤ fallback_threshold)
- **THEN** no `RadixSort` or `CompactUnique` passes are added to the render graph
- **AND** the fallback shader's output is returned directly

#### Scenario: Dedup dispatches for max_pairs, reads actual count from GPU

- **WHEN** the detector builds the dedup section of the render graph
- **THEN** `RadixSort::AddPasses` is called with `elem_capacity = max_pairs` and `pair_count_buf = gpu_pair_count`
- **AND** `CompactUnique::AddPasses` is called with `elem_capacity = max_pairs` and `pair_count_buf = gpu_pair_count`
- **AND** `GetVMAddress()` is NOT called on `gpu_pair_count` during RG build

#### Scenario: Zero pairs at execution time produces no work

- **WHEN** pair generation produces zero pairs (no collisions this frame)
- **THEN** all dedup shader threads check `idx >= pair_count.count` (0) and return immediately
- **AND** `unique_count` receives 0, which is copied to `pair_count`

### Requirement: Out-of-bounds shape exclusion in cell passes

The `count_cells.comp` and `fill_cells.comp` shaders SHALL each include an explicit out-of-bounds check that returns early for shapes whose AABB is entirely outside `[grid_world_min, grid_world_max]`. This check SHALL be in addition to the existing `global_flags` check.

The AABB computation shader (`compute_aabbs.comp`) SHALL write a degenerate (zero-size) AABB at `world_min` for out-of-bounds shapes and SHALL NOT set `global_flags` or append to `global_list`.

#### Scenario: Out-of-bounds shape excluded from count_cells

- **WHEN** shape `i` has AABB entirely outside the grid
- **THEN** `count_cells.comp` writes `shape_cell_count[i] = 0`
- **AND** no contribution to `total_assignments`

#### Scenario: Out-of-bounds shape excluded from fill_cells

- **WHEN** shape `i` has AABB entirely outside the grid
- **THEN** `fill_cells.comp` writes no `(cell_id, shape_index)` pairs for shape `i`
