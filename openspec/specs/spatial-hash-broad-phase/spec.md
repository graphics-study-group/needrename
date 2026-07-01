# spatial-hash-broad-phase

## Purpose

Govern the GPU spatial-hash broad-phase collision detection pipeline: AABB computation, two-pass cell assignment, counting sort by cell ID, within-cell pair generation, global-shape handling, configurable small-N fallback, and integration with `XPBDGpuSolver` via `SpatialHashBroadDetector`.

## Requirements

### Requirement: SpatialHashBroadDetector class

The `SpatialHashBroadDetector` class SHALL be an independent broad-phase collision detector that owns GPU compute pipelines and buffers for spatial-hash-based candidate pair generation. It SHALL follow the new detector pattern: lazy SPIR-V loading on first `Detect` call, `ComputeStage` ownership, `ComputeResourceBinding` management, and self-owned RenderGraph recording via a `Detect(vk::CommandBuffer cb)` method.

The constructor SHALL accept a `RenderSystem &` only. Sizing and configuration parameters SHALL be passed to `Configure()`. No GPU resources SHALL be allocated until `Configure` or `Detect` is first called.

The detector SHALL expose a two-phase API:
```cpp
void Configure(
    PhysicsScene &scene,
    uint32_t shape_count,
    GridConfig grid_config,
    uint32_t fallback_all_pairs_threshold
);
BroadDetectorOutputBuffers Detect(vk::CommandBuffer cb);
```

`Configure` SHALL cache `&scene` and all sizing parameters, ensure internal buffers are sized, and upload grid config and shape slot count to host-visible GPU buffers.

`Detect` SHALL lazily build the detector's own RenderGraph. The RG structure SHALL be determined at build time: if `shape_count <= fallback_all_pairs_threshold`, a fallback RG is built (AABBs → clear pair count → fallback all-pairs); otherwise the full spatial-hash RG is built (using ParallelScan utility for prefix sums). `Detect` SHALL import scene buffers directly from the cached `PhysicsScene*`, record all passes to `cb`, and return raw `ComputeBuffer*` references to the output pair buffers.

#### Scenario: Lazy initialization on first Detect call

- **WHEN** `SpatialHashBroadDetector::Detect(cb)` is called for the first time after `Configure`
- **THEN** the detector loads all broad-phase SPIR-V files from `<ENGINE_PHYSICS_SPIRV_DIR>/solver/SpatialHashBroadDetector/`
- **AND** creates `ComputeStage` and `ComputeResourceBinding` instances for each shader
- **AND** builds its RenderGraph and records it to `cb`
- **AND** subsequent calls reuse the same pipelines

#### Scenario: Detector exposes output buffers to narrow-phase

- **WHEN** `SpatialHashBroadDetector::Detect(cb)` completes
- **THEN** it returns a `BroadDetectorOutputBuffers` struct with raw `ComputeBuffer` references (`.pair_buffer`, `.pair_count_buffer`) and `.max_pairs`
- **AND** all buffers are owned by the detector and valid until the next call or detector destruction

#### Scenario: Detector returns empty result for insufficient shapes

- **WHEN** `Detect(cb)` is called with `shape_slot_count <= 1`
- **THEN** the detector writes `pair_count = 0` and returns without dispatching any compute passes

#### Scenario: Fallback RG built for small N

- **WHEN** `shape_count <= fallback_all_pairs_threshold` at RG build time
- **THEN** the detector builds a fallback RG (AABBs + all-pairs generation only)
- **AND** no cell assignment, counting sort, or within-cell generation passes are present in the RG

#### Scenario: Spatial-hash RG built for large N

- **WHEN** `shape_count > fallback_all_pairs_threshold` at RG build time
- **THEN** the detector builds the full spatial-hash RG with all passes
- **AND** ParallelScan is used as a utility function during RG build to add prefix sum passes

### Requirement: AABB computation per shape

The broad-phase detector SHALL compute an Axis-Aligned Bounding Box for each alive shape every frame. The AABB SHALL be computed from the shape's world position, world rotation, type, and feature vector using a dedicated compute shader (`compute_aabbs.comp`).

For box shapes, the AABB SHALL be the world-space bounding box of the oriented box. For sphere shapes, the AABB SHALL be `world_pos ± radius` in all axes. For cylinder shapes, the AABB SHALL conservatively bound the cylinder in world space using the maximum extent of the rotated cylinder.

The AABB SHALL be stored as `aabb_min[]` and `aabb_max[]` buffers (two `vec4` SSBOs, w component unused).

#### Scenario: Box AABB with identity rotation

- **WHEN** a box shape at (0, 0, 0) with half-extents (2, 1, 3) and identity rotation has its AABB computed
- **THEN** `aabb_min = (-2, -1, -3)` and `aabb_max = (2, 1, 3)`

#### Scenario: Sphere AABB

- **WHEN** a sphere shape at (5, 0, 0) with radius 2.0 has its AABB computed
- **THEN** `aabb_min = (3, -2, -2)` and `aabb_max = (7, 2, 2)`

#### Scenario: Dead shapes have zero AABB

- **WHEN** a shape slot is marked dead in `shape_alive`
- **THEN** the AABB computation shader writes zero to both `aabb_min` and `aabb_max` for that slot

### Requirement: Two-pass cell assignment

The detector SHALL assign each non-global alive shape to all spatial grid cells its AABB overlaps, using a two-pass scheme:

**Pass 1 (count_cells.comp)**: For each shape, compute the range of cell coordinates `[cell_min, cell_max]` that its AABB overlaps. If the shape is global, write 0 to `shape_cell_count[i]`. Otherwise write the cell count `(cell_max.x - cell_min.x + 1) * (cell_max.y - cell_min.y + 1) * (cell_max.z - cell_min.z + 1)` to `shape_cell_count[i]`. Atomically add the count to a global `total_assignments` counter.

**Prefix sum**: Compute exclusive prefix sum of `shape_cell_count[]` into `shape_cell_offset[]` using `ParallelScan`.

**Pass 2 (fill_cells.comp)**: For each non-global shape, re-compute the cell range and write `(cell_id, shape_index)` pairs at `cell_shape_pairs[shape_cell_offset[i] + local_idx]`. The cell ID SHALL be a 1D index computed as `cz * GRID_Y * GRID_X + cy * GRID_X + cx` where `(cx, cy, cz)` are grid cell coordinates.

#### Scenario: Shape in single cell

- **WHEN** a shape's AABB is fully contained within one cell at grid coordinate (3, 2, 1) in a 64×64×64 grid
- **THEN** `shape_cell_count[i] = 1`
- **AND** exactly one `(cell_id, i)` pair is written to `cell_shape_pairs`

#### Scenario: Shape spanning multiple cells

- **WHEN** a shape's AABB spans from cell (1, 1, 0) to cell (2, 2, 0) in a 64×64×64 grid (2×2×1 = 4 cells)
- **THEN** `shape_cell_count[i] = 4`
- **AND** four `(cell_id, i)` pairs are written consecutively starting at `shape_cell_offset[i]`

#### Scenario: Global shape excluded from cell assignment

- **WHEN** a shape's `global_flags[i] = 1`
- **THEN** `shape_cell_count[i] = 0` (count_cells writes zero for global shapes)
- **AND** no `(cell_id, i)` pairs are written for this shape in fill_cells

### Requirement: Global shape handling

Shapes whose AABB spans more than `max_cells_per_shape` cells SHALL be marked as global (`global_flags[i] = 1`). Shapes whose AABB is entirely outside `[grid_world_min, grid_world_max]` SHALL NOT be marked global — they SHALL be silently ignored (no pairs generated, not included in `global_list[]`). Global shapes SHALL be excluded from cell assignment, counting sort, and within-cell pair generation.

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

### Requirement: Counting sort by cell ID

The detector SHALL sort `(cell_id, shape_id)` pairs by cell ID using GPU counting sort, producing a `sorted_pairs[]` array where entries are grouped by cell ID in ascending order.

The counting sort SHALL proceed in three stages:
1. **Histogram**: Each thread processes one `(cell_id, shape_id)` pair and atomically increments `cell_histogram[cell_id]`
2. **Prefix sum**: Exclusive prefix sum of `cell_histogram[]` into `cell_offsets[]` via `ParallelScan` (in-place: input and output are the same buffer)
3. **Scatter**: `cell_scratch` is initialized from `cell_offsets` via `copy_uint.comp`. Each thread re-reads its pair and writes it to `sorted_pairs[atomic_add(cell_scratch[cell_id], 1)]`

#### Scenario: Three shapes in two cells

- **WHEN** `cell_shape_pairs` contains `[(cell_0, s1), (cell_1, s0), (cell_0, s3)]`
- **THEN** after counting sort `sorted_pairs` contains `[(cell_0, s1), (cell_0, s3), (cell_1, s0)]`
- **AND** `cell_offsets[cell_0]` indicates the start of cell_0's entries
- **AND** `cell_offsets[cell_1]` indicates the start of cell_1's entries

#### Scenario: Empty cells consume no entries

- **WHEN** no shape overlaps cell `k`
- **THEN** `cell_histogram[k] = 0` after histogram pass
- **AND** `cell_offsets[k] == cell_offsets[k+1]` after prefix sum (zero-width range)

### Requirement: Within-cell collision pair generation

The detector SHALL generate candidate collision pairs from the sorted cell-shape data in a compute shader (`generate_broad_pairs.comp`). For each cell `c`, all alive shapes assigned to that cell SHALL be paired in upper-triangle order (all pairs `(i, j)` with `i < j` within the cell's shape list).

The shader SHALL iterate over cells using `cell_offsets[]` to determine the range of shapes in each cell. Each workgroup processes multiple cells (one thread per cell). Global shapes are not included in this shader — they are handled by the separate `generate_global_pairs.comp`.

Each generated pair SHALL satisfy `index_a < index_b`. Dead shapes SHALL be excluded from pair generation (checked via `shape_alive`).

The shader SHALL additionally require that the two shapes' world-space AABBs overlap before emitting a candidate pair. The overlap test SHALL be a 3-axis separating-axis check using the `aabb_min[]` / `aabb_max[]` buffers produced by `compute_aabbs.comp`: the pair `(a, b)` is emitted only if `all(aabb_max[a] >= aabb_min[b])` and `all(aabb_max[b] >= aabb_min[a])` on all three XYZ axes. The shader SHALL bind `AabbMin` and `AabbMax` as `readonly buffer` and the C++ pass SHALL declare both reads via `UseBuffer(..., RR)` so the render graph inserts the read-after-write barrier from `compute_aabbs`.

The AABB overlap test SHALL be evaluated after the `shape_alive` check (so degenerate AABBs of dead shapes never enter the test) and before the collision-filter binary search (cheaper test first).

#### Scenario: Cell with three shapes produces three pairs

- **WHEN** cell 0 contains shapes with indices {2, 5, 7}
- **AND** all three shapes' AABBs pairwise overlap
- **THEN** the shader generates pairs (2, 5), (2, 7), (5, 7) for that cell

#### Scenario: Cell with one shape produces no pairs

- **WHEN** cell 0 contains only shape index 3
- **THEN** no pairs are generated for cell 0

#### Scenario: Dead shape excluded

- **WHEN** a shape in cell 0 is marked dead in `shape_alive`
- **THEN** no pair involving that shape is generated
- **AND** the cell's effective population for pair generation is reduced by 1

#### Scenario: Same-cell shapes with non-overlapping AABBs produce no pair

- **WHEN** shapes 2 and 5 share cell 0 (e.g. shape 2 spans multiple cells including cell 0, shape 5 sits in a corner of cell 0)
- **AND** their world-space AABBs do not overlap on at least one axis
- **THEN** the pair (2, 5) is not written to the output pair buffer

#### Scenario: AABB overlap test runs after alive check

- **WHEN** a shape in cell 0 is dead (AABB is degenerate `vec4(0)`)
- **AND** another alive shape in cell 0 has an AABB containing the origin
- **THEN** the dead shape is skipped by the `shape_alive` check before any AABB read
- **AND** no pair involving the dead shape is generated

### Requirement: Collision filter checking during pair generation

The `generate_broad_pairs.comp` shader SHALL skip any candidate pair `(a, b)` where `b` is in shape `a`'s filter list. The check SHALL use binary search over `shape_filter_data[shape_filter_offset[a] .. shape_filter_offset[a] + shape_filter_count[a]]` which is guaranteed sorted in ascending order. Since filter lists are symmetric (CPU-side guarantee), only one direction SHALL be checked.

#### Scenario: Filtered pair skipped

- **WHEN** shapes 3 and 7 share a cell and shape 3's filter list contains 7
- **THEN** the pair (3, 7) is not written to the output pair buffer

#### Scenario: Non-filtered pair emitted

- **WHEN** shapes 3 and 7 share a cell and neither filter list contains the other
- **THEN** the pair (3, 7) is written to the output pair buffer

### Requirement: Small-N fallback to all-pairs

When `shape_count <= fallback_all_pairs_threshold`, the detector SHALL skip the entire spatial-hash pipeline (cell assignment, counting sort, within-cell generation, global pair pass) and instead generate all upper-triangle pairs `(i, j)` with `i < j` directly via `generate_all_pairs_fallback.comp`. The AABB pass still runs (it produces the `aabb_min`/`aabb_max` buffers consumed by the overlap test). Filter checking SHALL still apply.

The fallback shader SHALL additionally require that the two shapes' world-space AABBs overlap before emitting a candidate pair, using the same 3-axis separating-axis check as the within-cell path. The shader SHALL bind `AabbMin` and `AabbMax` as `readonly buffer` and the C++ pass SHALL declare both reads via `UseBuffer(..., RR)`.

#### Scenario: Fallback triggered for small N

- **WHEN** a PhysicsScene has `shape_slot_count = 5` and `fallback_all_pairs_threshold = 8`
- **THEN** the detector generates all 10 upper-triangle pairs directly via the fallback shader
- **AND** no cell assignment, counting sort, or within-cell generation passes are dispatched

#### Scenario: Fallback not triggered for large N

- **WHEN** a PhysicsScene has `shape_slot_count = 100` and `fallback_all_pairs_threshold = 8`
- **THEN** the detector runs the full spatial-hash pipeline

#### Scenario: Fallback prunes non-overlapping pairs

- **WHEN** the fallback path enumerates pair (i, j) with `i < j`
- **AND** both shapes are alive and not collision-filtered
- **AND** their world-space AABBs do not overlap on at least one axis
- **THEN** the pair (i, j) is not written to the output pair buffer

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

The detector SHALL use its existing `ParallelScan` instance for `CompactUnique`'s internal prefix sum. The dedup SHALL be skipped if the fallback all-pairs path is used.

#### Scenario: Duplicate pairs are removed

- **WHEN** within-cell generation produces pairs `[(1,4), (2,3), (1,4)]` (pair (1,4) appears twice)
- **AND** no global pairs are generated
- **THEN** after dedup, `collision_pairs[]` contains `[(1,4), (2,3)]`
- **AND** `pair_count = 2`

#### Scenario: Dedup dispatches for max_pairs, reads actual count from GPU

- **WHEN** the detector builds the dedup section of the render graph
- **THEN** `RadixSort::AddPasses` is called with `elem_capacity = max_pairs` and `pair_count_buf = gpu_pair_count`
- **AND** `GetVMAddress()` is NOT called on `gpu_pair_count` during RG build

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

### Requirement: Grid config and threshold in XpbdConfig

`XpbdConfig` SHALL include the following new fields:
- `glm::vec3 grid_world_min` (default: `{-100, -100, -100}`)
- `glm::vec3 grid_world_max` (default: `{100, 100, 100}`)
- `float grid_cell_size` (default: `2.0`)
- `uint32_t max_cells_per_shape` (default: `8`)
- `uint32_t fallback_all_pairs_threshold` (default: `8`)

#### Scenario: XpbdConfig default values

- **WHEN** an `XpbdConfig` is default-constructed
- **THEN** `grid_world_min = (-100, -100, -100)`, `grid_world_max = (100, 100, 100)`
- **AND** `grid_cell_size = 2.0`, `max_cells_per_shape = 8`, `fallback_all_pairs_threshold = 8`

### Requirement: Pair output always satisfies index_a < index_b

All collision pairs emitted by the broad-phase detector SHALL satisfy `pair.x < pair.y`. The all-pairs fallback, within-cell generation, and global-shape pairing SHALL all independently enforce this invariant.

#### Scenario: Global shape pairs satisfy ordering

- **WHEN** global shape 5 is paired with shape 2
- **THEN** the emitted pair is (2, 5), not (5, 2)
