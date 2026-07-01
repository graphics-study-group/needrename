## MODIFIED Requirements

### Requirement: SpatialHashBroadDetector shader source layout

Broad-phase detector GLSL source files SHALL live under `engine/Physics/shader/solver/SpatialHashBroadDetector/`. The following shaders SHALL exist:

- `compute_aabbs.comp` — per-shape AABB computation and global-shape marking with compact global list appending
- `count_cells.comp` — first pass of two-pass cell assignment
- `fill_cells.comp` — second pass of two-pass cell assignment
- `histogram_cells.comp` — counting sort histogram pass
- `scatter_sort.comp` — counting sort scatter pass
- `generate_broad_pairs.comp` — within-cell upper-triangle pair generation with AABB overlap pruning and collision filter checking
- `generate_global_pairs.comp` — global-shape × all-shapes pair generation via 2D dispatch with AABB overlap pruning
- `generate_all_pairs_fallback.comp` — all-pairs fallback for small N with AABB overlap pruning
- `memset_uint.comp` — clears a uint buffer to zero (reused across passes)
- `copy_uint.comp` — copies a uint buffer (used for initializing atomic counters)

The three pair-generation shaders (`generate_broad_pairs.comp`, `generate_global_pairs.comp`, `generate_all_pairs_fallback.comp`) SHALL each bind `AabbMin` and `AabbMax` as `readonly buffer` (appended at the next free binding indices after their existing bindings) and SHALL perform a 3-axis separating-axis AABB overlap test before emitting any candidate pair. The `compute_aabbs.comp` shader (which produces these buffers) is unchanged.

#### Scenario: Broad-phase shaders compiled to SPIR-V

- **WHEN** the engine build completes
- **THEN** all 10 broad-phase shader SPIR-V files exist under `<ENGINE_PHYSICS_SPIRV_DIR>/solver/SpatialHashBroadDetector/`
- **AND** each was compiled from its corresponding `.comp` source

#### Scenario: Pair-generation shaders bind AABB buffers

- **WHEN** `generate_broad_pairs.comp`, `generate_global_pairs.comp`, or `generate_all_pairs_fallback.comp` is dispatched
- **THEN** each shader has `AabbMin` and `AabbMax` bound as `readonly buffer` at descriptor set 0
- **AND** the matching C++ pass declares `UseBuffer(aabb_min_h, RR)` and `UseBuffer(aabb_max_h, RR)` so the render graph inserts the read-after-write barrier from `compute_aabbs`
