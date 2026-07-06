## 1. Method renames

- [x] 1.1 Rename `XPBDGpuSolver::Step()` → `AddStepPasses()` in header and cpp
- [x] 1.2 Rename `SpatialHashBroadDetector::Step()` → `AddDetectPasses()` in header and cpp
- [x] 1.3 Rename `ConvexCollisionDetector::Step()` → `AddDetectPasses()` in header and cpp
- [x] 1.4 Update `PhysicsExampleRenderGraphBuilder.cpp` call site from `m_xpbd_solver->Step(...)` to `m_xpbd_solver->AddStepPasses(...)`

## 2. PhysicsScene handle struct

- [x] 2.1 Forward-declare `enum class RGBufferHandle : int32_t;` in `PhysicsScene.h` inside the `Engine` namespace
- [x] 2.2 Add `PhysicsSceneBufferHandles` struct in `PhysicsScene.h` alongside `PhysicsGpuBuffers`, with `RGBufferHandle` fields for `shape_alive`, `shape_type`, `shape_feature`, `shape_world_position`, `shape_world_rotation`, `shape_bound_rigid_body`, `shape_local_position`, `shape_local_rotation`, `shape_filter_offset`, `shape_filter_count`, `shape_filter_data`

## 3. Broad-phase detector output structs and AddDetectPasses() refactor

- [x] 3.1 Add `BroadDetectorOutputBuffers` and `BroadDetectorOutputHandles` structs to `SpatialHashBroadDetector.h`
- [x] 3.2 Replace `GetPairBuffer()`, `GetPairCountBuffer()`, `GetMaxPairs()` with a single `GetOutputBuffers()` returning `BroadDetectorOutputBuffers`
- [x] 3.3 Change `AddDetectPasses()` signature to accept `const PhysicsSceneBufferHandles &handles` and return `BroadDetectorOutputHandles`
- [x] 3.4 Update `SpatialHashBroadDetector.cpp`: remove all `builder.ImportExternalResource` calls for scene-owned buffers (`shape_alive`, `shape_type`, `shape_feature`, `shape_world_position`, `shape_world_rotation`), use `handles.*` instead
- [x] 3.5 Update `SpatialHashBroadDetector.cpp`: return `BroadDetectorOutputHandles` containing handles for `gpu_collision_pairs` and `gpu_pair_count` (already imported internally)
- [x] 3.6 Update `SpatialHashBroadDetector.cpp`: keep internal buffer imports (`gpu_aabb_min`, `gpu_aabb_max`, `gpu_global_flags`, cell buffers, etc.) — these are detector-private
- [x] 3.7 Update `SpatialHashBroadDetector.cpp`: resolve filter buffers (`filter_off_buf`, `filter_cnt_buf`, `filter_dat_buf`) from `PhysicsScene` pointers, use matching handles from `handles` struct (or import the dummy buffer if scene filters are null)

## 4. Narrow-phase detector handle struct and AddDetectPasses() refactor

- [x] 4.1 Add `NarrowDetectorOutputHandles` struct to `ConvexCollisionDetector.h` alongside `CollisionResultBuffers`
- [x] 4.2 Change `AddDetectPasses()` signature to accept `const PhysicsSceneBufferHandles &handles`, `RGBufferHandle pair_buffer_handle`, `RGBufferHandle pair_count_handle`, and return `NarrowDetectorOutputHandles`
- [x] 4.3 Update `ConvexCollisionDetector.cpp`: remove all `builder.ImportExternalResource` calls for scene-owned buffers, use `handles.*` instead
- [x] 4.4 Update `ConvexCollisionDetector.cpp`: use `pair_buffer_handle` and `pair_count_handle` directly in `UseBuffer` instead of re-importing
- [x] 4.5 Update `ConvexCollisionDetector.cpp`: return `NarrowDetectorOutputHandles` containing handles for detector-owned result buffers (`gpu_collision_ids`, `gpu_collision_normals`, `gpu_contact_point_a`, `gpu_contact_point_b`, `gpu_collision_count`)

## 5. Solver integration

- [x] 5.1 Add pre-import for `shape_type` and `shape_feature` buffers in `XPBDGpuSolver::AddStepPasses()` (currently only `shape_alive`, `shape_world_pos`, `shape_world_rot`, `shape_local_pos`, `shape_local_rot`, `shape2body` are pre-imported)
- [x] 5.2 Add pre-import for `shape_filter_offset`, `shape_filter_count`, `shape_filter_data` if they exist on the PhysicsScene
- [x] 5.3 Construct `PhysicsSceneBufferHandles` from pre-imported handles and pass to both detector `AddDetectPasses()` calls
- [x] 5.4 Consume `BroadDetectorOutputHandles` from `broad_detector->AddDetectPasses()` and pass `pair_buffer`/`pair_count` handles to `narrow_detector->AddDetectPasses()`
- [x] 5.5 Consume `NarrowDetectorOutputHandles` from `narrow_detector->AddDetectPasses()` — use returned handles directly for `coll_ids_h`, `coll_normals_h`, `coll_pta_h`, `coll_ptb_h`, `coll_cnt_h` instead of calling `builder.ImportExternalResource` on narrow detector output buffers
- [x] 5.6 Update `GetOutputBuffers()` call site in solver (was `GetPairBuffer()` / `GetPairCountBuffer()` / `GetMaxPairs()`) for raw buffer access needed by shader bindings

## 6. Build verification

- [x] 6.1 Run CMake build and fix any compilation errors
- [x] 6.2 Verify `physics_example` builds and runs without validation layer errors
- [x] 6.3 Run RenderDoc capture to confirm passes execute in correct order: UpdateShape → AABB → SpatialHash → Convex → Clear → Accum → Apply
