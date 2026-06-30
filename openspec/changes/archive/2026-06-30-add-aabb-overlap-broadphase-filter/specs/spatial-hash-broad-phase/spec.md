## MODIFIED Requirements

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
