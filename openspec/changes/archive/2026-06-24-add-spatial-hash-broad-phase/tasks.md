## 1. GPU Parallel Prefix Sum (Prerequisite)

- [x] 1.1 Write `parallel_scan.comp` shader (Blelloch work-efficient exclusive scan) under `engine/Physics/shader/solver/XPBDSolver/parallel_scan.comp` supporting both single-workgroup (≤1024 elements) and multi-level paths
- [x] 1.2 Add C++ helpers in `SpatialHashBroadDetector.cpp` (or a shared utility) to dispatch parallel_scan with correct workgroup count and buffer bindings
- [x] 1.3 Verify `parallel_scan.comp` builds to SPIR-V via CMake pipeline (requires build)
- [x] 1.4 Write focused tests: scan of size 16, 64, 256, 1024, 10000 with known input/output vectors (requires build)

## 2. XpbdConfig Expansion

- [x] 2.1 Add `glm::vec3 grid_world_min`, `glm::vec3 grid_world_max`, `float grid_cell_size`, `uint32_t max_cells_per_shape`, `uint32_t fallback_all_pairs_threshold` to `XpbdConfig` struct in `XPBDGpuSolver.h`
- [x] 2.2 Set defaults: world ±100, cell size 2.0, max_cells_per_shape 8, fallback threshold 32

## 3. Collision Filtering — Component & Reflection

- [x] 3.1 Add `REFL_SER_ENABLE std::vector<ObjectHandle> m_ignore_collision_objects{};` to `CollisionShapeComponent` in `CollisionShapeComponent.h`
- [x] 3.2 Regenerate reflection/serialization files: `CollisionShapeComponent.h.inc`, `23_registrar_impl_CollisionShapeComponent.h.inc`, `23_serialization_impl_CollisionShapeComponent.h.inc`
- [x] 3.3 Pass `m_ignore_collision_objects` from `CollisionShapeComponent::Awake()` to `PhysicsScene::RegisterCollisionShape()` (new parameter or separate setter)

## 4. Collision Filtering — PhysicsScene Storage & GPU Buffers

- [x] 4.1 Add CPU-side storage in `PhysicsScene`: `m_pending_filter_handles` (per-shape `vector<ObjectHandle>`), `m_shape_filter_offset`, `m_shape_filter_count`, `m_shape_filter_data` (flat sorted `uint32_t`)
- [x] 4.2 Implement `PhysicsScene::ResolveCollisionFilters(Scene &scene)` — resolve ObjectHandles to shape indices, enforce symmetry, sort per-shape filter lists
- [x] 4.3 Add GPU buffers in `PhysicsScene`: `m_gpu_shape_filter_offset`, `m_gpu_shape_filter_count`, `m_gpu_shape_filter_data`; create/update in `RefreshGpuBuffers()`
- [x] 4.4 Add GPU buffer pointers to `PhysicsGpuBuffers` struct: `shape_filter_offset`, `shape_filter_count`, `shape_filter_data`
- [x] 4.5 Implement filter cleanup in `UnregisterCollisionShape()` — remove stale references to the unregistered shape from all other shapes' filter data, rebuild GPU buffers
- [x] 4.6 Call `ResolveCollisionFilters()` at the appropriate point (requires integration in PhysicsSystem or example)

## 5. SpatialHashBroadDetector — C++ Class

- [x] 5.1 Create `engine/Physics/Collision/SpatialHashBroadDetector.h` with PImpl pattern: `GridConfig` struct, constructor (`RenderSystem&`, `GridConfig`, `fallback_all_pairs_threshold`), `Step(RenderGraphBuilder&, PhysicsScene&)`, `GetPairBuffer()`, `GetPairCountBuffer()`, grid validation
- [x] 5.2 Create `engine/Physics/Collision/SpatialHashBroadDetector.cpp` with `Impl` struct: lazy SPIR-V loading for 7 shaders, `ComputeStage`/`ComputeResourceBinding` per shader, all owned GPU buffers
- [x] 5.3 Implement `EnsureBuffers()` — allocate/resize AABB buffers, cell_shape_pairs, sorted_pairs, cell_histogram, cell_offsets, cell_scratch, collision_pairs, pair_count, global_flags, shape_cell_count, shape_cell_offset, grid_config uniform buffer
- [x] 5.4 Implement small-N fallback: when `shape_count <= fallback_all_pairs_threshold`, skip spatial hash and run `generate_all_pairs_fallback.comp` directly with filter checking
- [x] 5.5 Implement `Step()` — sequence all compute passes (AABB → count cells → prefix sum → fill cells → histogram → prefix sum → scatter → generate pairs), importing PhysicsScene buffers as external render-graph resources
- [x] 5.6 Add grid validation in constructor: reject > 2²⁰ cells with `std::runtime_error`

## 6. SpatialHashBroadDetector — Compute Shaders

- [x] 6.1 Write `compute_aabbs.comp` — per-shape AABB from world pos/rot/type/feature; set global_flags for shapes spanning > max_cells_per_shape cells or outside world bounds
- [x] 6.2 Write `count_cells.comp` — compute cell range from AABB, write `shape_cell_count[i]`, atomicAdd `total_assignments`
- [x] 6.3 Write `fill_cells.comp` — re-compute cell range, write `(cell_id, shape_index)` pairs at `cell_shape_pairs[shape_cell_offset[i] + local_idx]`; skip global shapes
- [x] 6.4 Write `histogram_cells.comp` — atomicAdd per-cell histogram from `cell_shape_pairs`
- [x] 6.5 Write `scatter_sort.comp` — scatter `cell_shape_pairs` to `sorted_pairs` using `cell_scratch` (copy of prefix sum) for atomic offsets
- [x] 6.6 Write `generate_broad_pairs.comp` — within-cell upper-triangle pair generation with binary-search filter checking, plus global-shape all-pairs pass
- [x] 6.7 Write `generate_all_pairs_fallback.comp` — all-pairs upper-triangle generation with filter checking for small-N fallback
- [x] 6.8 Verify all 7 shaders compile to SPIR-V and exist under `<ENGINE_PHYSICS_SPIRV_DIR>/solver/SpatialHashBroadDetector/` (requires build)

## 7. ConvexCollisionDetector Refactor

- [x] 7.1 Modify `ConvexCollisionDetector::Step()` to accept `const ComputeBuffer &pair_buffer` and `const ComputeBuffer &pair_count_buffer` as external pair inputs
- [x] 7.2 Remove `gpu_collision_pairs` ownership from `ConvexCollisionDetector::Impl` (pair buffer is now externally owned)
- [x] 7.3 Remove `pair_gen_stage`, `pair_gen_resource_binding`, `pair_gen_cached_spirv` from `Impl`
- [x] 7.4 Remove pair-generation pass from `Step()` (the pass that dispatched `generate_pairs.comp`)
- [x] 7.5 Add `pair_count` check: threads with `gl_GlobalInvocationID.x >= pair_count` return immediately in `detect_collisions.comp`
- [x] 7.6 Add `PairCount` buffer binding to `detect_collisions.comp` and the resource binding in `Step()`
- [x] 7.7 Update `CollisionResultBuffers` and `GetCollisionResultBuffers()` if needed (no structural change expected)

## 8. XPBDGpuSolver Integration

- [x] 8.1 Add `std::unique_ptr<SpatialHashBroadDetector> broad_detector` to `XPBDGpuSolver::Impl`
- [x] 8.2 Refactor `EnsureCollisionDetector` → `EnsureCollisionDetectors(uint32_t shape_count)` that creates/recreates both detectors; narrow detector's `max_pairs` sized from broad detector's output capacity
- [x] 8.3 In `Step()`, dispatch `broad_detector->Step(builder, physics_scene)` before `collision_detector->Step(builder, physics_scene, pair_buf, count_buf)`
- [x] 8.4 Retrieve broad-phase pair buffer and pair count, pass to narrow-phase; narrow-phase contacts dispatch uses broad-phase pair buffer capacity for workgroup count
- [x] 8.5 Recreate detectors when shape count changes (existing `cached_shape_count` logic, expanded for both)

## 9. Cleanup

- [x] 9.1 Delete `engine/Physics/shader/solver/ConvexCollisionDetector/generate_pairs.comp`
- [x] 9.2 Remove `generate_pairs.comp` reference from any build scripts or documentation (GLOB_RECURSE — no build script changes needed)
- [x] 9.3 Update `PhysicsExampleRenderGraphBuilder` to use new `XpbdConfig` fields if needed (requires build verification)
- [x] 9.4 Add `engine/Physics/Collision/` to `CMakeLists.txt` glob if not already covered (verify `SpatialHashBroadDetector.cpp` is picked up) — already covered by GLOB_RECURSE

## 10. Testing & Validation

- [x] 10.1 Test spatial hash broad-phase with a scene of 100+ shapes — verify pair count is significantly less than N×(N−1)/2
- [x] 10.2 Test global shape handling — create a shape larger than max_cells_per_shape cells, verify it pairs with all other shapes
- [x] 10.3 Test small-N fallback — scene with 5 shapes, verify all-pairs path is used
- [x] 10.4 Test collision filtering — set up ignore lists, verify filtered pairs are absent from broad-phase output
- [x] 10.5 Test filter symmetry — verify if A ignores B, B also ignores A in resolved filter data
- [x] 10.6 Test shape unregistration filter cleanup — unregister a shape, verify stale filter references are removed
- [x] 10.7 Test grid validation — attempt grid with > 1M cells, verify construction error
- [x] 10.8 Verify debug output still works from narrow-phase shader
- [x] 10.9 Run physics example with simulation enabled, verify visually stable behavior
