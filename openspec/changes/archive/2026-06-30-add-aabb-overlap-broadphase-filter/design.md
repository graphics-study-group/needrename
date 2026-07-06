## Context

`SpatialHashBroadDetector` runs a GPU broad-phase pipeline that produces candidate collision pairs for the narrow phase. Three shaders emit pairs today, none of which test whether the two shapes' world-space AABBs actually overlap:

- `generate_all_pairs_fallback.comp` — small-N path, all upper-triangle pairs.
- `generate_broad_pairs.comp` — within-cell upper-triangle pairs from the spatial hash.
- `generate_global_pairs.comp` — global shapes (AABB spans too many cells) × every alive shape.

The AABBs themselves are already computed every frame by `compute_aabbs.comp` into `gpu_aabb_min` / `gpu_aabb_max` (`vec4` SSBOs, `w` unused), and this pass runs in **all** paths including the fallback branch (it precedes the `if (use_fallback)` decision in `BuildRenderGraph`). The AABB data is available but unused by the pair-generation shaders.

The render graph tracks buffer accesses via `UseBuffer(handle, access)` per pass and `ImportExternalResource(buffer, prev_access)` at RG entry. `prev_access` describes only the cross-RG boundary state of a buffer; intra-RG transitions (write → read) are derived automatically from the chain of `UseBuffer` declarations. The AABB buffers are detector-private and are imported once with `prev_access = RW`.

## Goals / Non-Goals

**Goals:**

- Prune candidate pairs whose world-space AABBs do not overlap, in all three pair-generation shaders.
- Preserve all existing correctness invariants (alive checks, collision-filter checks, `index_a < index_b` ordering, dedup pipeline).
- Declare the new AABB reads to the render graph so the RAW barrier from `compute_aabbs` (WW) to the pair-generation readers (RR) is inserted correctly.

**Non-Goals:**

- Changing the spatial-hash structure, cell assignment, counting sort, or dedup pipeline.
- Replacing the spatial-hash cell test with a pure AABB sweep — the cell grouping stays as the primary candidate source; AABB overlap is an additional, subtractive filter layered on top.
- Resizing any buffer or changing `max_pairs` sizing (the filter only reduces pair count, so worst-case capacity remains safe).
- Touching the `prev_access` of any buffer import or changing public detector API.
- Modifying `compute_aabbs.comp` (it already produces the AABBs needed).

## Decisions

### Decision 1: AABB overlap test is a 3-axis separating-axis check inside each shader

Each pair-generation shader gains a shared `aabb_overlap(uint a, uint b)` helper:

```glsl
bool aabb_overlap(uint a, uint b) {
    return all(greaterThanEqual(aabb_max.v[a].xyz, aabb_min.v[b].xyz))
        && all(greaterThanEqual(aabb_max.v[b].xyz, aabb_min.v[a].xyz));
}
```

Two AABBs overlap iff each axis' max of one is `>=` the min of the other on all three axes.

**Rationale**: The test is branchless-ish (vector comparisons + `all`), cheap (6 vec4 reads + 6 comparisons), and conservative-correct — it never prunes a pair whose shapes actually collide, since colliding shapes always have overlapping AABBs.

**Alternatives considered**:
- *Skip the test for the within-cell path because same-cell shapes "should" overlap*: rejected — shapes assigned to multiple cells (large AABBs) routinely share a cell with shapes whose AABBs are far away in another part of that cell. The cell test is necessary but not sufficient; AABB overlap adds real pruning.
- *Move the test to a separate cull pass before pair generation*: rejected — it would require a new buffer and an extra dispatch, and the test is cheap enough to inline per candidate pair.

### Decision 2: Test placement — after alive check, alongside (before) the filter check

In all three shaders the order becomes: canonicalize/alive → **AABB overlap** → filter check → emit. The AABB test is placed before the binary-search filter check because the AABB test is cheaper (fixed 6 comparisons vs a log-scale binary search) and prunes the common non-proximate case first.

Dead shapes carry degenerate AABBs (`vec4(0)`), but every shader already short-circuits on `shape_alive` before reaching the AABB test, so degenerate AABBs never enter the overlap computation. This preserves the existing `shape_alive` guarantee and avoids touching it.

### Decision 3: Each of the three passes declares `UseBuffer(aabb_min_h, RR)` and `UseBuffer(aabb_max_h, RR)`

The render graph derives intra-RG barriers purely from `UseBuffer` declarations. Without declaring the read, the RG would not know the pair-generation passes depend on `compute_aabbs`'s write, and no barrier would be inserted — a data race reading potentially stale AABB data. Declaring both reads as `RR` (`ShaderRandomRead`) is mandatory and sufficient.

### Decision 4: `prev_access` on the AABB buffer imports is unchanged

The AABB buffers are imported once at the top of `BuildRenderGraph` with `prev_access = RW` (`SpatialHashBroadDetector.cpp:397-398`). This describes the cross-RG boundary state (what the previous frame's RG left the buffer in) and is independent of how many downstream passes read the buffer inside this RG. Adding RR readers downstream does not change the entry state, so `prev_access` stays `RW`. No other buffer's `prev_access` is affected: scene buffers are untouched, and no new buffers are introduced.

### Decision 5: No binding-index conflicts — append `AabbMin`/`AabbMax` after existing bindings

Each of the three shaders already has a fixed set of `layout(set=0, binding=N)` declarations. `AabbMin`/`AabbMax` are appended at the next free binding indices, and the matching `srb.BindBuffer("AabbMin", ...)` / `srb.BindBuffer("AabbMax", ...)` calls are added to each pass's `ComputeResourceBinding` block. Descriptor set 0 is the only set in use, consistent with the existing broad-phase shaders.

| Shader | Existing top binding | New bindings |
|---|---|---|
| `generate_all_pairs_fallback.comp` | 0–6 | 7 = `AabbMin`, 8 = `AabbMax` |
| `generate_broad_pairs.comp` | 0–11 | 12 = `AabbMin`, 13 = `AabbMax` |
| `generate_global_pairs.comp` | 0–9 | 10 = `AabbMin`, 11 = `AabbMax` |

## Risks / Trade-offs

- **[Degenerate AABB for out-of-grid shapes may emit a few false pairs]** → Shapes entirely outside the grid get a degenerate AABB at `world_min` (`compute_aabbs.comp:112-114`). If a global shape's AABB contains `world_min`, the overlap test passes and a pair is emitted; the narrow phase rejects it. This is conservative (no missed collisions) and bounded to the small set of out-of-grid alive shapes. Accepted; not worth a special-case in the shader.

- **[Per-pair cost increases slightly]** → Each emitted-or-rejected candidate now does 6 extra vec4 reads + comparisons. This is dwarfed by the savings of pruning pairs that would otherwise flow through narrow-phase GJK/EPA. Net win expected; no measurement required to proceed, but worth a RenderDoc pair-count comparison after implementation.

- **[Forgetting a `UseBuffer` declaration causes a silent data race]** → This is the highest-risk implementation bug because the RG will not error — it will simply omit a barrier. Mitigation: tasks explicitly require adding both `UseBuffer` calls to all three passes, and the verify step checks the RG pass list in RenderDoc for the expected `compute_aabbs` → pair-gen barrier.

- **[Binding index drift if shaders are reordered later]** → Mitigated by appending at the end and documenting the binding table in this design. Mirrors the existing convention in these shaders.
