# Collision filter refactor: ComponentHandle targeting with fixed-capacity GPU layout

The collision filter system is refactored from `ObjectHandle`-based (entire GameObject) to
`ComponentHandle`-based (specific `CollisionShapeComponent`) targeting. Filter symmetry is
guaranteed by `PhysicsAdaptor` at flush time rather than stored bilaterally. A fixed
per-shape capacity of 8 filter entries replaces the append-only dynamic array, eliminating
buffer growth and enabling simpler GPU shader access.

## Status

proposed

## Context

Previously, `CollisionShapeComponent::m_ignore_collision_objects` stored `ObjectHandle`s.
The Adaptor resolved each ObjectHandle by iterating *all* components on the target
GameObject, collecting every `CollisionShapeComponent` found. This made it impossible to
filter individual shapes when a GameObject carried multiple collision components. The
resolved shape indices were appended to a global `m_shape_filter_data` array in
`PhysicsScene::SubmitCollisionShape`, which grew without bound on repeated submissions
because old filter data was never reclaimed.

The GPU shader performed binary search on `filter_offset[shape]..filter_offset[shape]+filter_count[shape]`
and only checked one direction (the shape with the smaller index), relying on URDF loader
to manually mirror filter declarations in both parent and child links.

## Decision

**1. ComponentHandle targeting.** `m_ignore_collision_shapes` stores
`std::vector<ComponentHandle>`, each referring to a specific `CollisionShapeComponent`.
Resolution in `PhysicsAdaptor` is a single `dynamic_cast` or `GetComponent<T>` lookup.
Invalid handles log a warning and are skipped — the application is not terminated.

**2. Declarative storage, symmetric flush.** `PhysicsAdaptor` maintains
`m_filter_map: unordered_map<ComponentHandle, unordered_set<ComponentHandle>>` storing
only the component's own declarations. Symmetry is derived at flush time: for every
`(src, tgt)` declaration, the pair `{min(src_idx, tgt_idx), max(src_idx, tgt_idx)}` is
emitted. A greedy allocation fills up to `MAX_FILTER_ENTRIES` entries per shape; if either
side of a pair is full, the entire pair is discarded, preserving the invariant that A is
in B's filter list if and only if B is in A's.

**3. Fixed-capacity GPU layout (`MAX_FILTER_ENTRIES = 8`).** `m_shape_filter_data` is a
fixed-stride array: `data[shape_idx * MAX_FILTER_ENTRIES + k]`. `filter_offset` and
`filter_count` buffers are removed. Valid entries occupy the first N slots; remaining
slots hold `INVALID_INDEX (0xFFFFFFFF)`. The shader stops scanning on the first
`INVALID_INDEX`.

**4. Full rebuild each frame.** `PhysicsScene::SetShapeFilters` replaces the entire
`m_shape_filter_data` array via assignment. No append, no accumulation. `AllocateCollisionShapeSlot`
pre-allocates `MAX_FILTER_ENTRIES` entries per slot. `UnregisterCollisionShape` does not
clean filter data — it is overwritten on the next `SetShapeFilters` call.

**5. Stale filter cleanup policy.** `m_filter_map` is not cleaned on component
unregistration. During flush, entries whose source or target ComponentHandle resolves to
an invalid or non-alive shape index are skipped with a warning. Full cleanup is deferred
to a future refactor of the unregistration path.

**6. URDF loader.** Only the child link declares `ignore_collision_shapes` targeting the
parent link's collision components (single direction). Symmetry is the Adaptor's
responsibility.

## Considered Options

- **Bilateral (pushed) symmetry:** storing `m_filter_map[target].insert(src)` on every
  write. Rejected because it mixes last-write-wins with cross-contamination — a
  component's own `Init` could erase filters it did not declare.

- **Variable-length array with append.** Rejected because repeated submissions caused
  unbounded buffer growth. Fixed-capacity avoids this entirely.

- **Dynamic per-shape capacity.** Rejected because it required `filter_offset`/`filter_count`
  buffers on the GPU, extra indirection, and binary search in the shader. The constant
  `MAX_FILTER_ENTRIES = 8` is sufficient for all current use cases (URDF parent-child
  filtering, selective environment collision).

- **Keeping `ObjectHandle`-based filtering.** Rejected because GameObject-level
  granularity cannot distinguish individual collision shapes on the same object.

## Consequences

- `CollisionShapeComponent::m_ignore_collision_objects` renamed to `m_ignore_collision_shapes`
  and type changed to `std::vector<ComponentHandle>`. Serialized assets need migration.
- `filter_offset` and `filter_count` buffers removed from `PhysicsScene`, `PhysicsGpuBuffers`,
  `SpatialHashBroadDetector`, and three broad-phase shaders. Shader binding numbers shift.
- `CollisionShapeComDescriptor::ignore_shape_indices` field removed.
- `MAX_FILTER_ENTRIES` must stay in sync between `PhysicsScene.h` (C++ `constexpr`) and
  each broad-phase shader (`#define`).
- Any code that manually populated `m_ignore_collision_objects` with ObjectHandles must
  be updated to use ComponentHandles.
