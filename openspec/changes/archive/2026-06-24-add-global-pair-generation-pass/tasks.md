## 1. Shader: Compact global list during AABB

- [x] 1.1 Add `GlobalList` and `GlobalCount` SSBO bindings to `compute_aabbs.comp`
- [x] 1.2 In `compute_aabbs.comp` main(), append shape index to `global_list` via `atomicAdd(global_count[0], 1)` when marking `global_flags[i] = 1`

## 2. Shader: New dedicated global pair generation

- [x] 2.1 Create `generate_global_pairs.comp` with 2D dispatch `(ceil(N/64), G, 1)` and local size `(64, 1, 1)`
- [x] 2.2 Implement pair emission logic: dedup global×global, filter check, alive check, `atomicAdd` on `pair_count`
- [x] 2.3 Verify shader compiles to SPIR-V via CMake pipeline

## 3. Shader: Clean up fallback shader

- [x] 3.1 Remove `GlobalFlags` and `GlobalMode` bindings from `generate_all_pairs_fallback.comp`
- [x] 3.2 Remove `global_mode` branch logic from `generate_all_pairs_fallback.comp` main()
- [x] 3.3 Update shader header comment to reflect clean all-pairs-only purpose

## 4. C++: New buffer and compute stage infrastructure

- [x] 4.1 Add `gpu_global_list` (sized to `shape_count * sizeof(uint32_t)`) and `gpu_global_count` (single uint, host-visible) buffers to `SpatialHashBroadDetector::Impl`
- [x] 4.2 Add `global_pairs_stage`, `global_pairs_binding`, `global_pairs_spirv` members to `Impl`
- [x] 4.3 Load `generate_global_pairs.comp.spv` in `Impl::EnsureInitialized()`
- [x] 4.4 Remove `gpu_global_mode` buffer and all references from `Impl`

## 5. C++: Render graph pass wiring

- [x] 5.1 Import `gpu_global_list` and `gpu_global_count` as external render graph resources in `AddDetectPasses()`
- [x] 5.2 Add a `memset_uint` clear pass for `gpu_global_count` before the AABB pass (reuse existing memset stage)
- [x] 5.3 Add global pair generation pass after `generate_broad_pairs` pass: bind `global_list`, `global_count`, `global_flags`, `shape_alive`, `shape_slot_count`, filter buffers, `collision_pairs`, `pair_count`; dispatch `(ceil(shape_count/64), global_count_readback, 1)`
- [x] 5.4 Remove `GlobalMode` host-side write (`*addr = 0u`) from the fallback path — no longer needed since `GlobalMode` is removed
- [x] 5.5 Remove `gpu_global_mode` buffer handle from fallback shader bindings in C++

## 6. Build & verify

- [x] 6.1 Build the engine and verify no compilation errors
- [x] 6.2 Run physics example — verify shapes outside world bounds or spanning many cells produce collision pairs
- [x] 6.3 Verify fallback path (small-N) still produces correct all-pairs (no regression)
