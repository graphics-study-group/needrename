# Tasks: Physics push constants

## 1. Rhi push-constant infrastructure

- [x] 1.1 Add `push_constant_size` field to `SPLayout`; extend `SPLayout::Reflect` to aggregate push-constant block size via `get_declared_struct_size` (0 when none)
- [x] 1.2 In `ComputeStage::CreatePipeline`, declare `VkPushConstantRange{eCompute, 0, size}` when reflected size > 0; expose `ComputeStage::GetPushConstantSize()`
- [x] 1.3 Add template `Rhi::PushConstants(cb, stage, const T&)` to `ComputeHelpers` with `assert(sizeof(T) <= GetPushConstantSize())`
- [x] 1.4 Default `BindComputeResource`'s `slot` parameter to 0

## 2. Shader migration to per-shader push-constant blocks (binding holes kept)

- [x] 2.1 XPBD shaders: `integrate_forces.comp`, `update_velocities_from_pose.comp`, `accumulate_hinge_position.comp`, `accumulate_fixed_position.comp` declare `{ vec4 gravity_dt; }`; `clear_int_buffer.comp`, `snapshot_position.comp` declare `{ uint elem_count; }`
- [x] 2.2 `dummy_solver.comp` declares `{ vec4 gravity_dt; }`
- [x] 2.3 `detect_collisions.comp` declares `{ float contact_margin; uint shape_slot_count; }`
- [x] 2.4 SpatialHash shaders: `compute_aabbs.comp`, `count_cells.comp`, `fill_cells.comp`, `generate_broad_pairs.comp` declare grid + shape-slot-count block; `generate_all_pairs_fallback.comp`, `generate_global_pairs.comp` declare `{ uint shape_slot_count; }`; `memset_uint.comp` declares `{ uint elem_count; }`; new `copy_uint_push.comp` (push-constant ElemCount); `copy_uint.comp` keeps its SSBO `ElemCount` (CompactUnique binds the GPU-written `PairCount` buffer — count unknown at record time)
- [x] 2.5 `parallel_scan.comp` and `add_block_offset.comp` declare the `ScanParams` block (mode/data_offset/elem_count/block_offset)
- [x] 2.6 `radix_histogram.comp` and `radix_scatter.comp` declare the `RadixParams` block (only fields the shader reads); `radix_prefix_sum_256.comp` untouched

## 3. Physics C++ migration

- [x] 3.1 `XPBDGpuSolver`: default `slot_count`, remove `m_frame_counter`, delete 6 constant buffers (`gpu_uniforms` + 5 count buffers) with write sites, unify the two dispatch paths on `BindComputeResource(..., 0)`, push values at dispatch sites
- [x] 3.2 `DummySolver`: default `slot_count`, remove `m_frame_counter`, delete `gpu_uniforms`, push gravity/dt at dispatch
- [x] 3.3 `ParallelScan`: delete `param_pool`/`AcquireParamBuffer`/`ResetParamPool`, push `ScanParams` per dispatch (incl. recursive block-sums scan)
- [x] 3.4 `RadixSort`: delete both parameter pools + `gpu_const_256`, push `RadixParams` per pass and `elem_count` for the memset pass
- [x] 3.5 `CompactUnique`: default `slot_count`, remove `m_frame_counter`, delete `gpu_const_one` (ElemCount for memset/copy), push elem_count for the clear pass; flag/scatter/copy passes keep the SSBO `PairCount` binding (GPU-written)
- [x] 3.6 `SpatialHashBroadDetector`: default `slot_count`, remove `m_frame_counter`, delete `gpu_one`/`gpu_grid_cells_p1`/`gpu_grid_config`/`gpu_shape_slot_count` (all 4), switch `DispatchClear`/`DispatchCopy` to push-constant elem counts (copy via new `copy_uint_push.comp`), push at all dispatch sites
- [x] 3.7 `ConvexCollisionDetector`: default `slot_count`, remove `m_frame_counter`, delete `gpu_detector_config`/`gpu_shape_slot_count`/`gpu_one`, push config at dispatch (DetectorConfig fix)
- [x] 3.8 Add `static_assert` size guards for every C++ push struct; verify `PreGPUStep` performs no GPU-memory writes

## 4. Test coverage

- [x] 4.1 `shader_refl_test`: embed GLSL with push-constant blocks (vec4, uint, and mixed vec4+scalar variants) and assert reflected `push_constant_size` (16/4/20/36)
- [x] 4.2 `headless_compute_test`: embed a push-constant compute shader (`output = input + params.offset`), push at record time, assert read-back after `waitIdle`

## 5. Verification and review gate

- [x] 5.1 Build + full `ctest --preset debug` green (48 tests)
- [x] 5.2 Grep acceptance: no `AllocateResourceBinding(3)`, `m_frame_counter`, `param_pool`, `ResetParamPool`, or deleted constant-buffer names under `engine/Physics/`; no `% 3` rotation expressions
- [x] 5.3 Stop for user review of the diff (no commit yet); user validates physics behavior manually (`physics_example`, incl. DetectorConfig margin effect)
- [x] 5.4 On approval, commit the migration (user committed `cc85c1d0`; push-struct sizes corrected to declared sizes — no `alignas(16)`, static_asserts restored at 16/20/20/36)

## 6. Shader binding renumbering (final, after reconfirmation)

- [x] 6.1 After user reconfirms, renumber all physics shader descriptor bindings to consecutive 0..N (no holes)
- [x] 6.2 Rebuild + re-run `ctest`; grep acceptance that every physics shader's bindings are contiguous
