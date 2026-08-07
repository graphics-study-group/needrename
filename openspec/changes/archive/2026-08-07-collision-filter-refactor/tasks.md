## 1. Data type changes (Component and Descriptors)

- [x] 1.1 Rename `CollisionShapeComponent::m_ignore_collision_objects` to `m_ignore_collision_shapes` and change type from `vector<ObjectHandle>` to `vector<ComponentHandle>` in header and all `BuildDescriptor` code
- [x] 1.2 Update `CollisionShapeDescriptor::ignore_collision_objects` to `ignore_collision_shapes` with type `vector<ComponentHandle>` in `PhysicsDescriptors.h`
- [x] 1.3 Remove `CollisionShapeComDescriptor::ignore_shape_indices` field from `PhysicsDescriptors.h`
- [x] 1.4 Remove `ignore_shape_indices` write and `m_resolved_filters` usage in `PhysicsAdaptor::Flush()` Step 3

## 2. PhysicsScene API refactor

- [x] 2.1 Add `static constexpr uint32_t MAX_FILTER_ENTRIES = 8` to `PhysicsScene.h`
- [x] 2.2 Remove `m_shape_filter_offset` and `m_shape_filter_count` member vectors from class body
- [x] 2.3 Remove `m_gpu_shape_filter_offset` and `m_gpu_shape_filter_count` GPU buffer members from class body
- [x] 2.4 Change `m_shape_filter_data` semantics: pre-allocate `shape_count * MAX_FILTER_ENTRIES` entries per slot, initialized to `INVALID_INDEX`
- [x] 2.5 Add `void SetShapeFilters(const std::vector<uint32_t>& filter_data, uint32_t shape_count)` declaration
- [x] 2.6 Implement `SetShapeFilters`: replace `m_shape_filter_data` via assignment; assert `filter_data.size() == shape_count * MAX_FILTER_ENTRIES`
- [x] 2.7 Remove filter writing code from `SubmitCollisionShape` (the 3 lines at PhysicsScene.cpp:229-233)
- [x] 2.8 Update `AllocateCollisionShapeSlot` to push `MAX_FILTER_ENTRIES` `INVALID_INDEX` entries instead of `push_back(0u)` for offset and count
- [x] 2.9 Update `Clear()` to remove offset/count clear calls and reset filter_data properly
- [x] 2.10 Update `RefreshGpuBuffers`: remove `EnsureBuffer`/`EnqueueBufferSubmission` for offset and count buffers; ensure filter_data EnsureBuffer uses `m_shape_alive.size() * MAX_FILTER_ENTRIES`
- [x] 2.11 Update `GetGpuBuffers()`: remove `shape_filter_offset` and `shape_filter_count` from `PhysicsGpuBuffers` return struct
- [x] 2.12 Update `PhysicsGpuBuffers` struct: remove `shape_filter_offset` and `shape_filter_count` pointer fields
- [x] 2.13 Update `DebugPrint()` if it references filter count/offset

## 3. PhysicsAdaptor filter management

- [x] 3.1 Add `m_filter_map: unordered_map<ComponentHandle, unordered_set<ComponentHandle>>` to `PhysicsAdaptor` class
- [x] 3.2 In `Flush()`: before current Step 1, for each pending shape with non-empty `ignore_collision_shapes`, replace `m_filter_map[component_handle]` with the new declaration set
- [x] 3.3 Implement filter resolution in `Flush()`: iterate `m_filter_map`, resolve ComponentHandle → shape_index via `m_shape_component_to_index`, skip INVALID/non-alive shapes with warning
- [x] 3.4 Implement pair collection: for each `(src_idx, tgt_set)` build `unordered_set<uvec2>` of `{min, max}` pairs, exclude self-references
- [x] 3.5 Implement greedy symmetric allocation: sort pairs by `(min, max)`, accept pair only if both sides have < MAX_FILTER_ENTRIES, write to both per-shape lists, warn on skipped pairs
- [x] 3.6 Construct flat `filter_data` array: for each shape slot, fill `MAX_FILTER_ENTRIES` entries with sorted valid indices followed by INVALID_INDEX
- [x] 3.7 Call `m_physics_scene.SetShapeFilters(filter_data, shape_count)` at the end of Flush (before `SyncGpuBuffers`)
- [x] 3.8 Remove old `m_resolved_filters` member and all references

## 4. GPU shader changes

- [x] 4.1 Add `#define MAX_FILTER_ENTRIES 8` to `generate_broad_pairs.comp`
- [x] 4.2 In `generate_broad_pairs.comp`: remove `ShapeFilterOffset` (binding 9) and `ShapeFilterCount` (binding 10) buffer declarations; renumber `ShapeFilterData` to binding 9, `AabbMin` to 10, `AabbMax` to 11
- [x] 4.3 Replace `is_filtered` function in `generate_broad_pairs.comp` with linear scan over `MAX_FILTER_ENTRIES` entries using `INVALID_INDEX` as early-exit sentinel
- [x] 4.4 Repeat 4.1-4.3 for `generate_global_pairs.comp` (renumber bindings 7-11 accordingly)
- [x] 4.5 Repeat 4.1-4.3 for `generate_all_pairs_fallback.comp` (renumber bindings 4-8 accordingly)

## 5. SpatialHashBroadDetector C++ binding changes

- [x] 5.1 Remove `srb.BindBuffer("ShapeFilterOffset", ...)` from all 3 dispatch paths in `SpatialHashBroadDetector.cpp`
- [x] 5.2 Remove `srb.BindBuffer("ShapeFilterCount", ...)` from all 3 dispatch paths

## 6. URDF loader changes

- [x] 6.1 Rename `CollectCollisionObjectHandles` to `CollectCollisionComponentHandles` and change return type from `vector<ObjectHandle>` to `vector<ComponentHandle>`
- [x] 6.2 In `CollectCollisionComponentHandles`: remove the `break` — collect all CollisionShapeComponent handles per GameObject, not just the first
- [x] 6.3 Update `add_ignores` lambda to accept `vector<ComponentHandle>` and push to `m_ignore_collision_shapes` instead of `m_ignore_collision_objects`
- [x] 6.4 Remove the parent→child ignore line (`add_ignores(*parent_go, child_collision_gos)`); keep only child→parent

## 7. Verification

- [x] 7.1 Build verification: `cmake --build build` succeeds with no errors
- [ ] 7.2 Run existing tests and verify no regressions
- [ ] 7.3 Manual test: load a URDF robot, verify no self-collision between parent-child links
- [ ] 7.4 Manual test: create two shapes with mutual ignore, verify they do not collide
- [ ] 7.5 Manual test: create shape with >8 ignore targets, verify warning and symmetric truncation
