## Context

The current collision filter pipeline: collision components store `ObjectHandle`s → `PhysicsScene` resolves them to shape indices via Scene traversal → bilateral push for symmetry → append to a variable-length filter data array → GPU binary search. Six distinct pain points exist: (1) GameObject-level granularity prevents per-component targeting, (2) append-only data causes unbounded GPU buffer growth, (3) symmetry is fragile (URDF must mirror, general code may not), (4) binary search is unnecessary overhead for small filter counts, (5) `filter_offset`/`filter_count` buffers add binding complexity, (6) filter data lives in `CollisionShapeComDescriptor` mixing concerns.

## Goals / Non-Goals

**Goals:**
- Filter specific `CollisionShapeComponent` instances via `ComponentHandle`, not entire GameObjects
- Guarantee filter symmetry without requiring bilateral declarations from user code
- Eliminate unbounded filter data growth with fixed-per-shape capacity
- Simplify GPU shader: remove `filter_offset`/`filter_count` buffers, use linear scan over fixed stride
- Centralize filter logic in `PhysicsAdaptor`; `PhysicsScene` only holds flat arrays

**Non-Goals:**
- Dynamic per-shape capacity beyond the fixed 8 entries
- Filter cleanup on `UnregisterCollisionShape` (deferred to future refactor)
- Per-pair filter context (e.g., "disable only in narrow-phase but allow broad-phase")

## Decisions

### 1. Fixed capacity `MAX_FILTER_ENTRIES = 8`

Each shape receives exactly 8 `uint32_t` slots in the flat `filter_data` array. Unused slots hold `INVALID_INDEX`. This eliminates `filter_offset`/`filter_count` buffers entirely — the GPU computes `base = shape_idx * 8` directly.

**Alternative**: Variable-length with offset/count buffers. Rejected because append behavior caused unbounded growth and required extra GPU indirection.

**Rationale**: 8 entries covers all current use cases (URDF parent-child filtering rarely exceeds 2-4 ignore targets per shape). Exceeding 8 triggers a warning and truncation — the pair is dropped entirely if either side is full, preserving symmetry.

### 2. Declarative storage, symmetric flush

`PhysicsAdaptor::m_filter_map: unordered_map<ComponentHandle, unordered_set<ComponentHandle>>` stores only the declaring component's own ignore targets. During `Flush`, for each `(src, tgt)` declaration the unordered pair `{min(src_idx, tgt_idx), max(src_idx, tgt_idx)}` is emitted. Pairs are processed in sorted order; a pair is written to both shapes' filter slots only if both have remaining capacity. Otherwise the entire pair is skipped.

**Alternative**: Bilateral push (`m_filter_map[target].insert(src)` on every write). Rejected because it mixes last-write-wins with cross-contamination — a component re-`Init` with a smaller ignore list would incorrectly erase filters it did not declare.

**Rationale**: Deriving symmetry at flush time is O(K) where K is total filter entries. The flush already processes all pending shapes, so this adds no extra pass.

### 3. Full rebuild each frame

`PhysicsScene::SetShapeFilters(const vector<uint32_t>&, uint32_t shape_count)` replaces the entire `m_shape_filter_data` via `operator=`. No append, no accumulation.

**Alternative**: Incremental updates per shape. Rejected because it requires tracking which shapes changed, adds state management complexity, and the fixed-capacity layout makes full rebuild cheap (typical worst-case: 1000 shapes × 8 × 4 bytes = 32 KB).

### 4. ComponentHandle resolution in Adaptor

`m_shape_component_to_index: unordered_map<ComponentHandle, uint32_t>` already exists in `PhysicsAdaptor`. Resolution is a single `map.find(handle)` call. Invalid handles log a warning and are skipped.

**Alternative**: `PhysicsScene` performing resolution via `Scene`. Rejected because `Physics/` has a hard constraint against Framework headers.

### 5. Linear scan in shader instead of binary search

With maximum 8 entries, a linear scan completes in 8 iterations worst-case. The shader also checks `INVALID_INDEX` as an early-exit sentinel, so the common case (3-4 entries) completes in ~4 iterations. This removes the `filter_count` buffer and eliminates a `while` loop with branching from the shader.

**Alternative**: Binary search. Rejected because with N ≤ 8 it provides no meaningful speedup and requires the `filter_count` buffer.

## Risks / Trade-offs

- **[REFL serialization break]** `m_ignore_collision_objects` → `m_ignore_collision_shapes` + `ObjectHandle` → `ComponentHandle`. Serialized scenes and assets will fail to load with the old field name/type. Mitigation: accept as planned breaking change; no migration path since the old ObjectHandle-based semantics cannot be automatically translated to ComponentHandle.
- **[Capacity ceiling]** A shape that genuinely needs >8 ignore targets will lose filter pairs silently (with warning). Mitigation: `MAX_FILTER_ENTRIES = 8` is chosen because no current use case exceeds this. If it becomes a bottleneck, the constant can be increased — only PhysicsScene allocation and shader loop bound change.
- **[Stale m_filter_map entries]** ComponentHandles that are destroyed without unregistration accumulate in `m_filter_map` until the next Flush when they resolve to INVALID. Mitigation: accepted as deferred cleanup; documented with TODO comments.
- **[Shader binding renumbering]** Removing bindings 9 and 10 shifts AabbMin/AabbMax bindings in all three broad-phase shaders. Mitigation: C++ side binds by name (unaffected), only shader `layout(binding=N)` literals change.
