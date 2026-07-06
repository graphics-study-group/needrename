## Why

The `add-spatial-hash-broad-phase` change introduced global-shape marking (`global_flags[i] = 1` for shapes spanning too many cells or outside world bounds), but the pair-generation pipeline never actually emits pairs involving global shapes. The design doc and spec both describe a post-spatial-hash global pair pass, but this pass was never implemented — global shapes are silently excluded from collision detection. This is a correctness bug.

## What Changes

- **NEW**: `generate_global_pairs.comp` compute shader — efficiently generates `global_shape × all_shapes` pairs via 2D dispatch `(ceil(N/64), G, 1)`, avoiding the O(N²) all-pairs approach
- **MODIFIED**: `compute_aabbs.comp` — extended to write global shape indices into a compact `global_list[]` array when marking `global_flags[i] = 1`
- **MODIFIED**: `generate_all_pairs_fallback.comp` — stripped of all global-mode logic (`GlobalFlags`, `GlobalMode` bindings removed; `global_mode` branch removed); becomes a clean all-pairs generator
- **MODIFIED**: `SpatialHashBroadDetector::Impl` — new `gpu_global_list` and `gpu_global_count` buffers; removed `gpu_global_mode` buffer; new compute stage for `generate_global_pairs.comp`; global pair pass dispatched after spatial hash pair generation
- **MODIFIED**: `add-spatial-hash-broad-phase` delta spec — updated global shape handling requirement to reflect the compact-list + dedicated-shader approach instead of the unspecified all-pairs fallback

## Capabilities

### New Capabilities

- `global-pair-generation-pass`: Efficient O(G×N) generation of collision pairs involving global shapes (large or out-of-bounds) via a dedicated 2D-dispatched compute shader with compact global-shape indexing

### Modified Capabilities

- `spatial-hash-broad-phase`: Global shape handling requirement changes from "all-pairs upper-triangle pass after within-cell generation" to "compact global-shape index list built during AABB computation, consumed by a dedicated 2D-dispatched shader that pairs each global shape against all shapes"

## Impact

- **New files**: `engine/Physics/shader/solver/SpatialHashBroadDetector/generate_global_pairs.comp`
- **Modified files**: `compute_aabbs.comp`, `generate_all_pairs_fallback.comp`, `SpatialHashBroadDetector.h`, `SpatialHashBroadDetector.cpp`
- **No API changes**: `SpatialHashBroadDetector` public interface unchanged
- **No breaking changes**: The fallback shader changes are internal; fallback path still generates all pairs correctly (global shapes are a subset)
- **Depends on**: `add-spatial-hash-broad-phase` (shared GPU buffers, shader infrastructure)
