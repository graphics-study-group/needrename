## Why

The broad-phase detector (`SpatialHashBroadDetector`) currently emits candidate pairs based purely on spatial-hash cell co-tenancy (within-cell pairs), all-pairs enumeration (fallback path), or global-shape membership (global pairs). None of these paths test whether the two shapes' world-space AABBs actually overlap. This produces many false-positive pairs that the narrow phase must reject, wasting GPU work — especially for large cells where corner-dwelling shapes share a cell but never collide, and for global shapes whose huge cell span forces them against every alive shape regardless of proximity. A cheap 3-axis AABB overlap test at pair-emission time prunes these candidates before they reach the narrow phase.

## What Changes

- **Add AABB overlap pruning to `generate_all_pairs_fallback.comp`**: after the existing alive + collision-filter checks, skip emission when the two shapes' AABBs do not overlap. Bind `AabbMin`/`AabbMax` and declare the read access to the render graph.
- **Add AABB overlap pruning to `generate_broad_pairs.comp`**: after the within-cell alive + filter checks, skip emission when the two shapes' AABBs do not overlap. Bind `AabbMin`/`AabbMax` and declare the read access to the render graph.
- **Add AABB overlap pruning to `generate_global_pairs.comp`**: after the existing self/alive/dedup/filter checks, skip emission when the global shape's AABB and the target shape's AABB do not overlap. Bind `AabbMin`/`AabbMax` and declare the read access to the render graph.
- **Render-graph synchronization**: each of the three passes gains `UseBuffer(aabb_min_h, RR)` and `UseBuffer(aabb_max_h, RR)` so the RG inserts the correct read-after-write barrier from `compute_aabbs` (WW) to the pair-generation readers (RR). No new buffers, no `prev_access` change — the AABB buffers are detector-private and fully intra-RG; their `ImportExternalResource` `prev_access=RW` is unaffected by adding downstream readers.
- **No API changes**: `SpatialHashBroadDetector` public interface, buffer sizes, `max_pairs` sizing, and the dedup pipeline (RadixSort + CompactUnique) are untouched. The filter is purely subtractive — it only reduces emitted pair count, so existing worst-case sizing remains safe.

## Capabilities

### New Capabilities

<!-- None — this change only tightens existing pair-generation behavior. -->

### Modified Capabilities

- `spatial-hash-broad-phase`: within-cell pair generation (`generate_broad_pairs`) and the small-N fallback all-pairs path (`generate_all_pairs_fallback`) SHALL additionally require world-space AABB overlap between the two shapes before emitting a candidate pair.
- `global-pair-generation-pass`: global-shape × alive-shape pair generation (`generate_global_pairs`) SHALL additionally require world-space AABB overlap between the global shape and the target shape before emitting a candidate pair.
- `physics-gpu-shaders`: the three broad-phase pair-generation shaders SHALL bind `AabbMin`/`AabbMax` buffers (already produced by `compute_aabbs.comp`) and read them as `readonly` to perform the overlap test.

## Impact

- **Affected shaders**: `engine/Physics/shader/collision/SpatialHashBroadDetector/generate_all_pairs_fallback.comp`, `generate_broad_pairs.comp`, `generate_global_pairs.comp`. Each gains an `aabb_overlap(a, b)` helper and two `readonly buffer` declarations, plus the overlap call in its emission path.
- **Affected C++**: `engine/Physics/Collision/SpatialHashBroadDetector.cpp` — three `ComputeResourceBinding::GetShaderResourceBinding()` blocks gain `BindBuffer("AabbMin", *gpu_aabb_min)` / `BindBuffer("AabbMax", *gpu_aabb_max)`, and the three `RenderGraphPassBuilder` chains gain `UseBuffer(aabb_min_h, RR)` / `UseBuffer(aabb_max_h, RR)`.
- **No new buffers**: `gpu_aabb_min`/`gpu_aabb_max` already exist and are computed by `compute_aabbs` in all paths (including fallback, since `compute_aabbs` runs before the `if (use_fallback)` branch).
- **No sync regression**: the only synchronization requirement is declaring the AABB reads via `UseBuffer` so the RG's dependency analysis inserts the RAW barrier from `compute_aabbs` (WW) to the readers (RR). `prev_access` on the AABB buffer imports is unchanged.
- **No API/breaking changes**: detector public API, `PhysicsScene` buffer handles, narrow-phase detector, and solver integration are unaffected.
- **Edge case**: dead shapes get degenerate AABBs (`vec4(0)`), but all three shaders already short-circuit on the `shape_alive` check before reaching the AABB test, so degenerate AABBs never participate in overlap tests. Shapes entirely outside the grid get a degenerate AABB at `world_min`; these may emit a small number of false pairs against global shapes whose AABB contains `world_min`, conservatively accepted (narrow phase rejects).
