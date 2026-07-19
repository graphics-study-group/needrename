## Why

The collision filter system uses `ObjectHandle` (GameObject-level) for ignore targeting, stores filter data in an append-only array causing unbounded GPU buffer growth, performs asymmetric binary search in shaders, and requires manual symmetry mirroring in URDF loader code. These issues compound into fragile, unpredictable filtering behavior that cannot target individual collision shapes on multi-shape GameObjects.

## What Changes

- **BREAKING**: `CollisionShapeComponent::m_ignore_collision_objects: vector<ObjectHandle>` renamed to `m_ignore_collision_shapes: vector<ComponentHandle>`, targeting specific `CollisionShapeComponent` instances instead of entire GameObjects
- **BREAKING**: `CollisionShapeDescriptor::ignore_collision_objects` field type changed from `vector<ObjectHandle>` to `vector<ComponentHandle>`
- **BREAKING**: `CollisionShapeComDescriptor::ignore_shape_indices` field removed — filter data no longer flows through per-shape submit
- Filter resolution moves from `PhysicsScene` to `PhysicsAdaptor`, where ComponentHandle is resolved to shape index via `m_shape_component_to_index` map
- Symmetry is guaranteed by `PhysicsAdaptor` at flush time via unordered pair deduplication and greedy allocation with bilateral capacity check — a pair is only accepted if both shapes have room
- GPU layout changes from variable-length `filter_offset` + `filter_count` + `filter_data` to fixed-stride `filter_data[shape * MAX_FILTER_ENTRIES + k]` with `MAX_FILTER_ENTRIES = 8`, padded with `INVALID_INDEX`
- GPU shader `is_filtered` replaces O(log n) binary search with linear scan over at most 8 entries
- `filter_offset` and `filter_count` GPU buffers and bindings removed from `PhysicsScene`, `PhysicsGpuBuffers`, `SpatialHashBroadDetector`, and three broad-phase shaders
- `PhysicsScene::SetShapeFilters` replaces append behavior with full rebuild each frame via assignment
- `UnregisterCollisionShape` no longer cleans filter references; stale entries are resolved to `INVALID_INDEX` at flush time
- URDF loader only sets child→parent ignore (single direction); symmetry is Adaptor's responsibility

## Capabilities

### New Capabilities

None — this is a refactor of an existing capability.

### Modified Capabilities

- `collision-filtering`: All requirements rewritten — ComponentHandle targeting, Adaptor-managed symmetry, fixed-capacity GPU layout with linear scan, full-rebuild filter submission, no unregistration cleanup

## Impact

| Area | Impact |
|------|--------|
| `CollisionShapeComponent.h/cpp` | Field rename + type change; serialized assets break |
| `PhysicsDescriptors.h` | `CollisionShapeDescriptor` field type change; `CollisionShapeComDescriptor` field removal |
| `PhysicsAdaptor.h/cpp` | New `m_filter_map`, filter resolution + symmetry logic, `SetShapeFilters` call |
| `PhysicsScene.h/cpp` | Remove 3 filter data members + 2 GPU buffers; new `SetShapeFilters`; `AllocateCollisionShapeSlot` pre-allocates fixed slots |
| `PhysicsGpuBuffers` | Remove `shape_filter_offset` and `shape_filter_count` fields |
| `SpatialHashBroadDetector.cpp` | Remove 6 `BindBuffer` calls for offset/count |
| `generate_broad_pairs.comp` | Remove 2 bindings, renumber, replace `is_filtered` |
| `generate_global_pairs.comp` | Same |
| `generate_all_pairs_fallback.comp` | Same |
| `UrdfLoader.cpp` | Collect ComponentHandle, single-direction ignore |
