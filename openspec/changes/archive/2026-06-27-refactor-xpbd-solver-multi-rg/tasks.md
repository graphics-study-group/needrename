## 1. Refactor ConvexCollisionDetector to Configure/Detect API

- [x] 1.1 Change constructor to accept only `RenderSystem &`
- [x] 1.2 Add `Configure(PhysicsScene &scene, uint32_t max_collision_pairs, float contact_margin, const ComputeBuffer &pair_buffer, const ComputeBuffer &pair_count_buffer)` method: cache `&scene`, `&pair_buffer`, `&pair_count_buffer`, ensure result buffers sized, upload `contact_margin` uniform
- [x] 1.3 Add `Detect(vk::CommandBuffer cb)` method: lazy-build own RenderGraph, import scene buffers from cached `PhysicsScene*` with correct `prev_access`, add clear + MPR detect passes, record to `cb`
- [x] 1.4 Return `CollisionResultBuffers` from `Detect` (raw `ComputeBuffer*` refs only, no handles)
- [x] 1.5 Remove `AddDetectPasses(builder, scene, handles, pair_h, count_h)`, `NarrowDetectorOutputHandles`, `GetCollisionResultBuffers` getter
- [x] 1.6 Add `IsInitialized()` and lazy shader loading in `Detect` (move from old `EnsureInitialized`)

## 2. Refactor SpatialHashBroadDetector to Configure/Detect API

- [x] 2.1 Change constructor to accept only `RenderSystem &`
- [x] 2.2 Add `Configure(PhysicsScene &scene, uint32_t shape_count, GridConfig grid_config, uint32_t fallback_all_pairs_threshold)` method: cache `&scene` and all params, ensure internal buffers sized, upload grid config + shape_slot_count to GPU
- [x] 2.3 Add `Detect(vk::CommandBuffer cb)` method: lazy-build own RenderGraph
- [x] 2.4 At RG build time, decide fallback vs spatial-hash path based on `shape_count <= fallback_all_pairs_threshold`
- [x] 2.5 Import scene buffers from cached `PhysicsScene*` with correct `prev_access` in both paths
- [x] 2.6 Use ParallelScan as utility function during RG build (pass-adding, not RG ownership)
- [x] 2.7 Return `BroadDetectorOutputBuffers` from `Detect` (raw `ComputeBuffer*` refs, no handles)
- [x] 2.8 Remove `AddDetectPasses(builder, scene, handles)`, `BroadDetectorOutputHandles`, `GetOutputBuffers` getter
- [x] 2.9 Ensure fallback path (shape_count <= 1) early-returns with zero pair count

## 3. Rename and restructure XPBDGpuSolver → XpbdGpuSolver as ISolver

- [x] 3.1 Rename class to `XpbdGpuSolver`, move to `ISolver` base class
- [x] 3.2 Change constructor to accept only `RenderSystem &` (PhysicsScene via `m_bound_scene`)
- [x] 3.3 Implement `PreGPUStep()`: lazy shader load, ensure intermediate buffers, upload uniforms, call `broad_detector->Configure()`, retrieve broad output buffer pointers, call `narrow_detector->Configure()` with pair buffer refs
- [x] 3.4 Implement `GPUStep(vk::CommandBuffer cb)`: lazy-build all 6 RGs, record in sequence with CPU loops
- [x] 3.5 Implement `IsInitialized()`: returns true after first `PreGPUStep` shader load
- [x] 3.6 Define `GpuStateSnapshot` struct for RG rebuild detection (body_count, max_contacts, hinge_joint_count, fixed_joint_count, shape_count)
- [x] 3.7 Remove `AddStepPasses(builder, scene, external_mm_handle)` and old `RenderGraphBuilder &` pattern

## 4. Build solver-owned RenderGraphs

- [x] 4.1 Implement `BuildPreCollisionRG()`: snapshots (pre-gravity, pre-contact, substep-start) + integrate forces + update shape world poses. All `ImportExternalResource` with precise `prev_access = {AT::None}` (first in chain per substep)
- [x] 4.2 Implement `BuildPostCollisionPreIterRG()`: clear Lagrange multipliers (contact + hinge + fixed, conditional on joint counts). `prev_access` reflects detector output states
- [x] 4.3 Implement `BuildPositionIterRG()`: accum contact pos + accum hinge pos (conditional) + accum fixed pos (conditional) + apply body pos. All mutable buffers use `prev_access = RW` (conservative for loop re-recording)
- [x] 4.4 Implement `BuildPostPositionRG()`: update velocities from pose delta. `prev_access` reflects PositionIterRG final state (RW for pos, RR for others)
- [x] 4.5 Implement `BuildVelocityIterRG()`: accum contact velocity + apply body velocity. Mutable buffers use `prev_access = RW` (conservative). Read-only buffers use precise `prev_access`
- [x] 4.6 Implement `BuildModelMatrixRG()`: write model matrices from body pose. Always built, always recorded. `prev_access = RR` for pose buffers
- [x] 4.7 Forward-declare `RGBufferHandle` and avoid `RGAttachmentDesc.h` include in header

## 5. Wire GPUStep recording sequence

- [x] 5.1 In `GPUStep`, loop `substep_count` times:
  - `PreCollisionRG.RecordAllPasses(cb)`
  - `broad_detector->Detect(cb)` → cache output `BroadDetectorOutputBuffers`
  - `narrow_detector->Detect(cb)` → cache output `CollisionResultBuffers`
  - `PostCollisionPreIterRG.RecordAllPasses(cb)`
  - Loop `pos_iters` times: `PositionIterRG.RecordAllPasses(cb)`
  - `PostPositionRG.RecordAllPasses(cb)`
  - Loop `vel_iters` times: `VelocityIterRG.RecordAllPasses(cb)`
- [x] 5.2 After substep loop: `ModelMatrixRG.RecordAllPasses(cb)`
- [x] 5.3 Call `render_system.GetSceneDataManager().SetModelMatricesBuffer(gpu.model_matrices)` on RG build
- [x] 5.4 Handle simulation toggle: all passes check `IsSimulationEnabled()` in dispatch lambda; time_step=0 when paused; ModelMatrixRG always dispatches

## 6. Manual prev_access tracing documentation

- [x] 6.1 Add comment block at top of `XpbdGpuSolver.cpp` documenting the per-buffer prev_access chain across all RGs (pos, rot, linvel, angvel, shapes, collision results, Lagrange, deltas, joints)
- [x] 6.2 Verify in code review that every `ImportExternalResource` call has a documented rationale for its `prev_access` value
- [x] 6.3 Mark loop RGs (PositionIterRG, VelocityIterRG) with comments explaining conservative `prev_access = RW` strategy

## 7. Clean up PhysicsScene and handle forwarding types

- [x] 7.1 Remove `PhysicsSceneBufferHandles` struct from `PhysicsScene.h` (no longer needed)
- [x] 7.2 Remove `BroadDetectorOutputHandles` and `NarrowDetectorOutputHandles` from detector headers
- [x] 7.3 Remove `RGBufferHandle` forward declaration from headers that no longer use it

## 8. Update example and tests

- [x] 8.1 Update `physics_example/main.cpp`: replace `DummySolver` or add `XpbdGpuSolver` registration alongside it
- [x] 8.2 Verify `physics_registration_test.cpp` compiles and passes with new `XpbdGpuSolver`
- [x] 8.3 Verify full build succeeds with no regressions

## 9. Verification

- [x] 9.1 Run Vulkan validation layers with synchronization checks enabled, verify no barrier hazards
- [x] 9.2 Verify correct physics behavior: bodies fall, collide, joints hold, simulation toggle works
- [x] 9.3 Verify model matrices are written correctly (objects visible in rendering)
- [x] 9.4 Verify RG rebuild triggers correctly when body count or joint count changes
- [x] 9.5 Verify loop RGs produce correct results with multiple iterations (pos_iters > 1, vel_iters > 1)
