## Context

The `add-spatial-hash-broad-phase` change introduced a spatial-hash broad-phase pipeline. Shapes spanning too many cells or outside world bounds are marked "global" (`global_flags[i] = 1` in `compute_aabbs.comp`). These shapes are correctly excluded from cell assignment, counting sort, and within-cell pair generation — but the final step of emitting pairs involving global shapes was never implemented.

The fallback shader (`generate_all_pairs_fallback.comp`) was written with a `global_mode` parameter intended for this purpose, but:
1. It dispatches O(N²/2) threads regardless of how many global shapes exist (wasteful for typical scenes with few global shapes)
2. It was never actually dispatched from the spatial hash path

This design proposes removing the fallback's global-mode logic and implementing a dedicated, efficient global pair generation pass.

## Goals / Non-Goals

**Goals:**
- Generate all collision pairs involving global shapes (correctness fix)
- Achieve O(G×N) dispatch where G = number of global shapes (typically << N)
- Compact global shape indexing during AABB computation (single atomic write per global shape)
- Clean separation: fallback shader handles small-N all-pairs; dedicated shader handles global pairs
- Deduplicate global×global pairs (each emitted exactly once)

**Non-Goals:**
- Sorting or deduplication of the full output pair buffer (separate change)
- Changing how within-cell pairs are generated
- Handling the case where global-mode fallback is still needed (it's not — the new shader handles all global cases)

## Decisions

### Decision 1: Compact global-shape index list built during AABB pass

**Chosen**: `compute_aabbs.comp` appends each global shape's index to `global_list[atomicAdd(global_count, 1)]`. The buffer is sized to `shape_count` (worst case: all shapes global). A `memset_uint` pass clears `global_count` before each frame's AABB pass.

**Alternatives considered**:
- **Post-process scan of `global_flags[]`**: Requires an additional prefix-sum + scatter pass just to build the compact list. The atomic append during AABB is effectively free (global shapes are rare in typical scenes).
- **No compact list, iterate `global_flags[]` in pair shader**: The pair shader would need to scan the full `global_flags[]` array to find global shapes, adding complexity and poor memory access patterns.

**Rationale**: Building the compact list during AABB computation adds negligible cost (one atomic per global shape). The compact list enables a clean 2D dispatch in the pair shader with exactly G workgroups in Y.

### Decision 2: 2D dispatch `(ceil(N/64), G, 1)` for global pair generation

**Chosen**: `generate_global_pairs.comp` dispatches with `gl_WorkGroupID.y` selecting a global shape from `global_list[]` and `gl_GlobalInvocationID.x` selecting the target shape. Deduplication of global×global pairs is handled by only emitting from the smaller global index.

```
Thread (s=gl_GlobalInvocationID.x, g_idx=gl_WorkGroupID.y)
  g = global_list[g_idx]
  if (s >= shape_count) return
  if (s == g) return
  if (!shape_alive[s]) return
  if (global_flags[s] && g > s) return  // dedup: smaller global emits
  emit (min(g,s), max(g,s))
```

**Alternatives considered**:
- **Reuse fallback shader in global-only mode**: O(N²/2) threads, most early-exit. For N=500 with G=5, dispatches 125K threads vs 5×500=2.5K for this approach. Wasteful and requires the `GlobalMode` mechanism we want to remove.
- **1D dispatch with loop**: `(ceil(N/64), 1, 1)` dispatch, each thread loops over `global_list[]`. Poor memory coalescing — adjacent threads access different global shapes, and the inner loop over global shapes wastes warp execution when G is small.

**Rationale**: 2D dispatch maps naturally to the problem. X dimension provides memory coalescing (adjacent threads read adjacent target shapes). Y dimension isolates each global shape to its own workgroup row, making the shader logic trivial (no loops).

### Decision 3: Remove GlobalMode from fallback shader

**Chosen**: Strip `GlobalFlags` and `GlobalMode` bindings from `generate_all_pairs_fallback.comp`. The fallback shader reverts to a clean all-pairs upper-triangle generator with filter checking only.

**Alternatives considered**:
- **Keep GlobalMode for potential reuse**: Dead code that could confuse future readers. The new global pass shader handles all global cases; there is no scenario where the fallback's global-mode is needed.
- **Delete fallback shader entirely and always use spatial hash**: The spatial hash has overhead even for small N. The fallback threshold optimization is valuable for scenes with few shapes.

**Rationale**: Clean separation of concerns. Fallback = all-pairs for small N. Global pass = global×all pairs for large N. No mode flags, no conditional dispatch.

### Decision 4: Shared output buffer and pair count

**Chosen**: Both within-cell and global pair passes write to the same `collision_pairs[]` buffer using `atomicAdd(pair_count[0], 1)`. A single clear of `PairCount` precedes both passes. No barrier or ordering guarantee is needed between them — they are serialized via the render graph's pass ordering.

**Rationale**: Simplicity. The output is a single contiguous buffer of all candidate pairs, consumed by narrow-phase. No need for separate offsets or merging passes.

## Risks / Trade-offs

### Risk 1: Global shapes outside world bounds may still produce many pairs → Mitigation
If a user has many shapes outside world bounds, G is large and the dispatch is expensive. However, this is user configuration error — world bounds should encompass all physics objects. The engine's default world bounds (±100m) are generous.

### Risk 2: Atomic contention on `global_count` during AABB pass → Mitigation
In practice, global shapes are rare (typically < 1% of shapes). Atomic contention on `global_count` is negligible. In the worst case (all shapes global), contention is bounded by the 64-thread workgroup size.

### Risk 3: `global_list` buffer sized for worst case → Mitigation
The buffer is `shape_count × sizeof(uint32_t)` bytes. For 1000 shapes, this is ~4 KB — negligible.

## Open Questions

*None — all decisions resolved during exploration.*
