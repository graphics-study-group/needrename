## 1. RadixSort algorithm shaders

- [x] 1.1 Create `radix_histogram.comp` — per-pass histogram shader. 64-thread workgroup, reads `uvec2` pairs, extracts byte from `.x` or `.y` based on `RadixParams.word_select`, atomically increments `histogram[digit]`. Dispatch: `ceil(elem_count / 64)`.
- [x] 1.2 Create `radix_prefix_sum_256.comp` — 256-thread single-workgroup exclusive prefix sum on the histogram. Uses shared memory Blelloch scan (same algorithm as `parallel_scan.comp` core). No UBO params needed (always 256 elements).
- [x] 1.3 Create `radix_scatter.comp` — per-pass scatter shader. Reads input pairs, extracts digit, `pos = atomicAdd(histogram[digit], 1)`, writes pair to `output[pos]`. Dispatch: `ceil(elem_count / 64)`.

## 2. CompactUnique algorithm shaders

- [x] 2.1 Create `flag_unique.comp` — marks each element as unique if `i == 0 || pairs[i] != pairs[i-1]`. Writes `1` or `0` to `unique_flags[i]`. Dispatch: `ceil(elem_count / 64)` with local size 64.
- [x] 2.2 Create `compact_scatter.comp` — scatters unique entries. Reads `flags[i]` (original) and `offsets[i]` (prefix-sum value). If `flags[i] == 1`, writes `pairs[i]` to `pairs[offsets[i]]`. Writes total unique count to `count_buf[0]`. Dispatch: `ceil(elem_count / 64)`.

## 3. RadixSort C++ module

- [x] 3.1 Create `engine/Physics/gpu_algorithm/RadixSort.h` — public header with `RadixSort` class declaration. API: constructor `(RenderSystem&, uint32_t max_elem_count)`, static `GetRequiredScratchBytes()`, static `GetRequiredTempPairsBytes(uint32_t)`, `AddPasses(...)`, `IsInitialized()`, `GetMaxElemCount()`. Constant `kMaxShapeCount = 1 << 20`.
- [x] 3.2 Create `engine/Physics/gpu_algorithm/RadixSort.cpp` — implementation with `Impl` struct (PIMPL). Include: `ComputeStage` ownership for 3 shaders, SPIR-V bytecode storage, param pool for per-dispatch `RadixSortParams` buffers, lazy `EnsureInitialized()`, `AddPasses` orchestration (8-pass loop with ping-pong swaps). Reuse existing `memset_uint.comp` for histogram clearing.
- [x] 3.3 Implement `RadixSortParams` GPU struct — `{ byte_shift, word_select, elem_count, _pad }` (16 bytes). Each of the 24 dispatches (8 passes × 3 steps) gets its own buffer from the pool.
- [x] 3.4 Implement `AddPasses` ping-pong logic — passes 0-3 sort by `.y` (word_select=0), passes 4-7 sort by `.x` (word_select=1). After 8 passes, final result in `pairs_buf_a`. Validate `max_shape_count <= kMaxShapeCount`, throw on exceed.

## 4. CompactUnique C++ module

- [x] 4.1 Create `engine/Physics/gpu_algorithm/CompactUnique.h`
- [x] 4.2 Create `engine/Physics/gpu_algorithm/CompactUnique.cpp`


## 5. Broad-phase shader modifications (out-of-bounds handling)

- [x] 5.1 Modify `compute_aabbs.comp` — remove `outside` from global condition. For out-of-bounds shapes: write degenerate AABB (`aabb_min = aabb_max = wmin`), set `global_flags[i] = 0`, do NOT append to `global_list`. Only `num_cells > max_cells` triggers global.
- [x] 5.2 Modify `count_cells.comp` — add out-of-bounds check after `global_flags` check: compute `wmax`, check `any(lessThan(mx, wmin)) || any(greaterThan(mn, wmax))`, return early if outside.
- [x] 5.3 Modify `fill_cells.comp` — same out-of-bounds check as `count_cells.comp`, return early if outside.

## 6. Broad-phase C++ integration

- [x] 6.1 Add new GPU buffers to `SpatialHashBroadDetector::Impl`: `gpu_pairs_temp` (ping-pong, `max_pairs × sizeof(uvec2)`), `gpu_radix_scratch` (1 KB), `gpu_unique_flags` (`max_pairs × sizeof(uint32_t)`), `gpu_unique_count` (`sizeof(uint32_t)`).
- [x] 6.2 Create `RadixSort` and `CompactUnique` instances in `Impl`, lazy-initialized on first spatial-hash RG build. Size them with `max_pairs`.
- [x] 6.3 Add dedup passes to `BuildRenderGraph()` — after global pairs pass, import new buffers into RG. Call `RadixSort::AddPasses` (pairs → sorted, ping-pong with temp). Call `CompactUnique::AddPasses` (sorted → unique compact). Only for spatial-hash path (not fallback).
- [x] 6.4 Ensure `pair_count` is updated by the dedup step — `CompactUnique` writes the new unique count, which replaces the atomic counter value in `gpu_pair_count`.

## 7. Build system

- [x] 7.1 Add 5 new shader `.comp` files to CMake GLSL compilation targets (same pattern as existing `algorithm/` shaders). Output SPIR-V to `build/engine/Physics/spirv/algorithm/`. (Auto-discovered via `GLOB_RECURSE`)
- [x] 7.2 Add `RadixSort.cpp` and `CompactUnique.cpp` to CMake source lists under `engine/Physics/gpu_algorithm/`. (Auto-discovered via `GLOB_RECURSE`)
- [x] 7.3 Verify build succeeds with all new files.

## 8. Verification

- [x] 8.1 Manual test with a scene containing shapes that share multiple grid cells — verify `pair_count` is lower after dedup and no duplicate pairs exist.
- [x] 8.2 Manual test with a shape flying outside grid bounds — verify it produces zero collision pairs.
- [x] 8.3 Manual test with global shapes (spanning > max_cells_per_shape) — verify they still generate correct pairs with all shapes.
- [x] 8.4 Verify fallback path (small N) still works correctly without dedup.
- [x] 8.5 Verify shape count > 2^20 throws `std::runtime_error` from `RadixSort`.
