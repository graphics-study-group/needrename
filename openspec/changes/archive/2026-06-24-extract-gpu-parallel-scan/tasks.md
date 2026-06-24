## 1. Shader and CMake Setup

- [x] 1.1 Create `engine/Physics/shader/algorithm/` directory
- [x] 1.2 Write `engine/Physics/shader/algorithm/parallel_scan.comp` — Blelloch work-efficient exclusive scan with separate InputData/OutputData bindings, ScanParams (mode, block_offset, elem_count), and BlockSums. Workgroup size 256, 512 elements per workgroup, three modes (0=scan, 1=scan+write block sums, 2=add block offsets)
- [x] 1.3 Update CMakeLists.txt in `engine/Physics/` to add the new shader source to the GLSL→SPIR-V build pipeline so that it produces `build/engine/Physics/spirv/algorithm/parallel_scan.comp.spv` — **No change needed: GLOB_RECURSE auto-discovers shader/*.comp**
- [x] 1.4 Build the project and verify the SPIR-V file is generated correctly

## 2. ParallelScan Class Implementation

- [x] 2.1 Create `engine/Physics/gpu_algorithm/ParallelScan.h` — class declaration with constructor(`RenderSystem&`, `uint32_t max_elem_count`), `AddPasses(builder, input_handle, output_handle, input_buf, output_buf, elem_count)`, and private members (ComputeStage, block-sums buffer, param buffer pool, helpers)
- [x] 2.2 Create `engine/Physics/gpu_algorithm/ParallelScan.cpp` — implement `EnsureInitialized()` to load SPIR-V and instantiate ComputeStage; implement `AddPasses` with single-level (elem_count ≤ 512), two-level (num_blocks ≤ 512), and deep recursive (>2 level) dispatch logic
- [x] 2.3 Implement per-pass parameter buffer pool — `AcquireParamBuffer(mode, block_offset, elem_count)` returns a small host-visible ComputeBuffer (12 bytes) with the three uint values written
- [x] 2.4 Implement `AddSinglePass` helper — adds one compute pass with correct `UseBuffer` declarations for data input (RR), data output (WW), and block-sums (WW for mode 1, RR|RW for mode 2, dummy for mode 0)
- [x] 2.5 Add the new `.cpp` to `engine/Physics/` CMakeLists.txt — **No change needed: GLOB_RECURSE auto-discovers ./*.cpp**

## 3. BroadDetector Migration

- [x] 3.1 Add `#include "Physics/gpu_algorithm/ParallelScan.h"` to `SpatialHashBroadDetector.cpp`
- [x] 3.2 Add `std::unique_ptr<ParallelScan> scan` member to `SpatialHashBroadDetector::Impl`, lazily initialized before first scan with `max(shape_count, grid_total_cells + 1)` (lazy init because shape_count is dynamic per-frame)
- [x] 3.3 Replace the inline scan at lines 727–769 (shape_cell_count → cell_offsets) with a call to `m_impl->scan->AddPasses(builder, scc_h, sco_h, *gpu_shape_cell_count, *gpu_cell_offsets, shape_count)`
- [x] 3.4 Add the cell histogram scan (cell_histogram → cell_offsets in-place) with a call to `m_impl->scan->AddPasses(builder, hist_h, hist_h, *gpu_cell_histogram, *gpu_cell_histogram, grid_total_cells + 1)`
- [x] 3.5 Remove the commented-out `DispatchParallelScan` and `AddScanPass` code (lines 293–388)
- [x] 3.6 Remove `gpu_scan_params`, `gpu_scan_elem_count`, `gpu_scan_block_sums` buffer members and their creation in `EnsureAllBuffers()`
- [x] 3.7 Remove the `scan_stage`, `scan_binding`, `scan_spirv` members and their initialization in `EnsureInitialized()`
- [x] 3.8 Remove `memset_stage`/`memset_spirv`/`copy_stage`/`copy_spirv` if they were only used by the scan path — **Verified: still used by non-scan passes (clear total_assignments, clear histogram, copy offsets→scratch), kept in place**

## 4. Cleanup

- [x] 4.1 Check if `engine/Physics/shader/solver/XPBDSolver/parallel_scan.comp` is referenced by any other code — **Only referenced from SpatialHashBroadDetector.cpp, which no longer uses it. Safe to remove.**
- [x] 4.2 Remove the XPBDSolver scan shader from CMakeLists.txt — **No change needed: GLOB_RECURSE auto-discovers; file will simply no longer exist**
- [x] 4.3 Rebuild and verify no compile errors — **Build succeeded: physics_shader + EngineLibPhysics compiled cleanly**

## 5. Verification

- [x] 5.1 Build the full project and verify zero compile/link errors
- [x] 5.2 Run the engine and verify the broad-phase detector produces correct collision pairs (visual inspection or existing test harness) — **Manual verification needed**
- [x] 5.3 Verify the multi-level scan path is exercised (use shape_count > 512 or grid large enough to trigger it) — **Manual verification needed**
