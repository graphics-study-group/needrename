## Context

The engine's GPU physics pipeline currently performs O(N²) all-pairs collision detection: `generate_pairs.comp` enumerates every shape pair `(i, j)` with `i < j`, then `detect_collisions.comp` runs the full MPR narrow-phase algorithm on every pair. The MPR algorithm involves iterative support-function queries, tetrahedron construction, and perturbation-based manifold generation — computationally expensive per pair. For N=500 shapes, ~125K pairs undergo this process each substep, regardless of spatial proximity.

We add a spatial-hash broad-phase filter that runs before narrow-phase. AABBs are computed per shape, shapes are assigned to a uniform 3D grid, and only shapes sharing at least one cell are tested. Additionally, per-shape collision filtering (ignore lists) is introduced so specific object pairs can be excluded from collision altogether.

The existing codebase patterns guide the implementation: lazy SPIR-V loading, PImpl idiom, `ComputeStage` ownership, render-graph integration via `AddDetectPasses()`, and SoA GPU buffer layout.

## Goals / Non-Goals

**Goals:**
- Reduce narrow-phase pairs from O(N²) to O(N × average_cell_population²) in typical scenes
- Independent `SpatialHashBroadDetector` class, testable in isolation
- `ConvexCollisionDetector` refactored to accept external pair buffer as input
- Configurable grid parameters in `XpbdConfig`
- Small-N fallback to all-pairs (avoids broad-phase overhead when N is tiny)
- Large-shape "global" marking for shapes spanning too many cells
- Per-shape collision ignore lists resolved from ObjectHandle to shape index on CPU, enforced symmetrically
- Reusable GPU parallel prefix sum via `ParallelScan` class

**Non-Goals:**
- Dynamic grid resolution (grid is fixed per scene)
- Hierarchical broad-phase (BVH, octree)
- Continuous collision detection (CCD)
- Trigger volumes or sensor shapes
- Multi-scene or streaming-world grid support

## Decisions

### Decision 1: Spatial hashing with uniform grid + counting sort

**Chosen**: Uniform 3D grid with configurable world bounds and cell size. Cell assignment uses a two-pass scheme (count per shape, prefix sum, fill). Pairs are grouped by cell via GPU counting sort (histogram + prefix sum + scatter).

**Alternatives considered**:
- **Radix sort**: More GPU passes (8 for 32-bit with 4-bit radix), memory-efficient for large grids. Overkill when grid ≤ 128³ (histogram ≈ 8 MB).
- **BVH**: Better for non-uniform distributions but requires tree construction and traversal on GPU, significantly more complex.
- **Sweep-and-prune**: Works well for temporal coherence but relies on sorted axis arrays and insertion sort; spatial hash is simpler to parallelize on GPU.

**Rationale**: Counting sort is simpler to implement, debug, and verify. The histogram memory cost is bounded (grid is validated at construction to ≤ 2²⁰ cells). The uniform grid handles the common case (objects distributed across a bounded world) well.

### Decision 2: Two-pass cell assignment with GPU prefix sum

**Chosen**: First pass computes `shape_cell_count[i]` (how many cells shape `i` overlaps) and atomically increments a global `total_assignments` counter. A GPU prefix sum over `shape_cell_count` produces `shape_cell_offset[i]`. Second pass re-computes the cell range for each shape and writes `(cell_id, shape_index)` pairs at `cell_shape_pairs[shape_cell_offset[i] + local_idx]`.

**Rationale**: The two-pass approach allocates exactly the right number of entries — no memory wasted on `max_cells_per_shape` padding. The cell range computation (AABB → min/max cell coordinates) is cheap enough to run twice. The prefix sum is handled by the `ParallelScan` class (`engine/Physics/gpu_algorithm/ParallelScan.h`).

**Buffer sizing**: `cell_shape_pairs` is sized to `shape_count * max_cells_per_shape` (conservative upper bound).

### Decision 3: Global shapes indexed into compact list, paired via dedicated shader

**Chosen**: Shapes whose AABB spans more than `max_cells_per_shape` cells, or whose center falls outside world bounds, are marked "global" (`global_flags[i] = 1`) and excluded entirely from the spatial hash pipeline (no cell assignment, no counting sort). During AABB computation, each global shape's index is atomically appended to a compact `global_list[]` buffer (tracked by `global_count`).

After within-cell pair generation, a dedicated `generate_global_pairs.comp` shader generates pairs between each global shape and every other alive shape using a 2D dispatch `(ceil(N/64), G, 1)`. `gl_WorkGroupID.y` selects the global shape; `gl_GlobalInvocationID.x` selects the target shape. Global×global pairs are deduplicated by only emitting from the smaller global index.

**Rationale**: This avoids duplicates — a global shape might otherwise appear both in a cell-based pair and in an explicit all-pairs fallback. It also bounds the `cell_shape_pairs` array size: non-global shapes contribute at most `max_cells_per_shape` entries each. The 2D dispatch is O(G×N) where G is typically small.

### Decision 4: Broad-phase max_pairs derived from narrow-phase capacity

**Chosen**: `SpatialHashBroadDetector` constructor takes `max_pairs` as an explicit parameter (not internally computed). The caller (`XPBDGpuSolver::EnsureCollisionDetectors`) derives it as `narrow_max_contacts / 5`, where `narrow_max_contacts = min(shape_count × (shape_count - 1) / 2 × 5, config.max_contact_points)`. This ensures the broad-phase pair buffer is exactly sized for the narrow-phase's worst-case output.

**Rationale**: The pair buffer serves as input to narrow-phase. Sizing it from narrow-phase capacity prevents over-allocation and makes buffer sizing explicit and traceable to the solver config.

### Decision 5: ConvexCollisionDetector receives external pair buffer

**Chosen**: `ConvexCollisionDetector::AddDetectPasses()` accepts external pair buffer and pair count buffer (plus their pre-imported render graph handles and scene buffer handles). CPU dispatch uses `max_collision_pairs` workgroups; shader threads beyond actual pair count early-return. A clear pass zeroes `collision_count` before detection each frame.

**Alternatives considered**:
- **Indirect dispatch**: GPU-driven dispatch count avoids over-dispatching but requires an indirect dispatch buffer and an additional compute pass to write it. Added complexity for modest savings.
- **Keep pair generation inside ConvexCollisionDetector**: Would duplicate pair-generation logic or couple broad/narrow detectors. Violates single responsibility.

**Rationale**: Fixed dispatch with early-return is the existing pattern (`collision_count` guard in contact solvers). It's simple, proven, and avoids indirect dispatch complexity. The pre-imported handle pattern avoids redundant `ImportExternalResource` calls.

### Decision 6: Collision filtering — ObjectHandle resolution and symmetry

**Chosen**: `CollisionShapeComponent` stores `std::vector<ObjectHandle> m_ignore_collision_objects`. On Awake, the raw ObjectHandles are passed to `PhysicsScene`. A separate `ResolveCollisionFilters(Scene &)` method (called after all GameObjects are Awake'd) resolves each ObjectHandle to the shape index of its directly-attached `CollisionShapeComponent` (not through rigid bodies). Each GameObject is expected to have exactly one `CollisionShapeComponent`, yielding a 1:1 ObjectHandle → shape_index mapping. The resulting per-shape filter lists are:
1. Made symmetric: if shape A filters shape B, then shape B also filters shape A
2. Sorted in ascending order for GPU binary search
3. Stored as flat `shape_filter_data[]` with per-shape `shape_filter_offset[]` and `shape_filter_count[]`

GPU pair generation checks `is_filtered(a, b)` via binary search; since data is symmetric, only one direction is checked.

**Rationale**: Deferred resolution solves the Awake ordering problem. CPU-side symmetry ensures GPU only checks one direction (halving filter lookups). Sorted arrays enable O(log K) binary search on GPU.

### Decision 7: Filter data lifecycle on shape unregistration

**Chosen**: When a shape is unregistered, all other shapes' filter lists are scanned and references to the removed shape are removed. The filter data arrays are rebuilt.

**Rationale**: Without cleanup, a stale filter reference could incorrectly filter a newly-registered shape that reuses the same slot index. Since unregistration is infrequent (object destruction), the O(N×K) rebuild cost is acceptable. Slot reuse with generation counters was considered but adds complexity to the allocation system.

### Decision 8: GPU prefix sum via ParallelScan class

**Chosen**: Implement `parallel_scan.comp` using the Blelloch work-efficient exclusive scan algorithm, located at `engine/Physics/shader/algorithm/parallel_scan.comp`. The C++ side provides a `ParallelScan` class at `engine/Physics/gpu_algorithm/ParallelScan.h` that encapsulates pipeline creation and multi-level dispatch.

Each workgroup processes 512 elements (256 threads × 2 loads per thread). For N ≤ 512, a single pass (mode=0) does the full scan in shared memory. For N > 512, the class dispatches:
1. mode=1: scan each 512-element block, write per-block sums to scratch
2. mode=0: recursively scan the block-sums region
3. `add_block_offset.comp`: add prefix-summed block offsets back to data

The scan operates on `uint` buffers and is used at three points in the broad-phase pipeline:
1. `shape_cell_count` → `shape_cell_offset` (N ≤ max_shapes)
2. `cell_histogram` → `cell_offsets` (N ≤ grid_cells + 1, up to ~1M)
3. Not needed for cell pair counts (within-cell generation iterates cells directly via cell_offsets)

## Risks / Trade-offs

### Risk 1: Dense scenes degrade to O(N²)
If many shapes cluster in one cell (e.g., a pile of objects), within-cell pair generation still produces O(K²) pairs where K is the cell population. → **Mitigation**: The cell size is configurable; users can reduce cell size to split clusters. The worst case is no worse than current all-pairs. A future enhancement could add a secondary spatial hash within dense cells.

### Risk 2: Counting sort memory for large grids
Grid dimensions are computed from world bounds / cell size. If a user sets a tiny cell size (e.g., 0.01m) in a large world (1000m), the grid could have 10¹² cells. → **Mitigation**: Construction-time validation rejects grids with > 2²⁰ (~1M) cells. If exceeded, construction throws `std::runtime_error` with a clear message suggesting larger cell size or smaller world bounds.

### Risk 3: GPU prefix sum is a new primitive with correctness risk
Parallel prefix sum is notoriously tricky to get right (off-by-one in scatter, bank conflicts). → **Mitigation**: Write focused unit tests for `parallel_scan.comp` using known input/output vectors. Verify with small sizes (16, 64, 256, 512, 10000 elements) before integrating into the pipeline.

### Risk 4: Filter data explosion
If every shape ignores every other shape, filter data is O(N²). → **Mitigation**: Document the intended use case (ignoring a few specific objects, not bulk filtering). In practice, ignore lists are small (typically < 10 entries). No hard limit is enforced but extreme cases will cause GPU memory exhaustion.

### Risk 5: Extra compute passes add frame time for small N
For N=10 shapes, the multi-pass broad-phase pipeline may be slower than the current 2-pass all-pairs approach. → **Mitigation**: The `fallback_all_pairs_threshold` config (default 8) causes the broad detector to skip spatial hashing entirely and generate all-pairs directly when `shape_count ≤ threshold`.

### Risk 6: Shapes outside world bounds
Shapes whose AABB center falls outside `[grid_world_min, grid_world_max]` cannot be assigned to valid cells. → **Mitigation**: Such shapes are marked "global" and paired with all other shapes via the dedicated `generate_global_pairs.comp`. This is a safe default but may degrade performance if many shapes are outside bounds. Users should configure world bounds to encompass all physics objects.

## Open Questions

*None — all open questions resolved.*

1. **Collision filter on joints**: Out of scope. Joint-connected bodies will still generate contacts at the joint limit regardless of filter settings. Users who want to disable collisions between joint-connected bodies must manually add the relevant ObjectHandles to the ignore list on both sides.

2. **`parallel_scan.comp` type support**: `uint` only. All scan inputs in the broad-phase pipeline are non-negative counts. If signed support is ever needed by other systems, it can be added as a separate entry point or specialization constant at that time.
