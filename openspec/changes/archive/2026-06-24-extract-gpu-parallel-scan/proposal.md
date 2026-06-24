## Why

The `SpatialHashBroadDetector` currently has a working single-level parallel prefix sum (line 731) but the multi-level scan orchestration (`DispatchParallelScan`) is broken and commented out due to two fundamental defects: (1) multiple dispatch passes sharing a single `gpu_scan_params` host-visible buffer causes an overwrite race where all passes read the last-written CPU values instead of their own parameters, and (2) repeated `ImportExternalResource` calls for the same physical buffer create duplicate render-graph handles that prevent correct barrier insertion. Without multi-level scan, the broad-phase detector cannot handle shape counts exceeding 512 or grid cell counts exceeding 512, capping the spatial hash at ~8×8×8 cells.

## What Changes

- Extract the parallel prefix-sum GPU algorithm into a standalone reusable class `ParallelScan` under `engine/Physics/gpu_algorithm/`
- Create a dedicated scan shader at `engine/Physics/shader/algorithm/parallel_scan.comp` with separate input/output buffer bindings and per-dispatch parameter storage-buffer uniforms
- Each scan dispatch receives its own tiny host-visible parameter buffer (mode, block_offset, elem_count), eliminating the overwrite race
- The class owns an internal scratch buffer for block-sum intermediates and allocates per-pass parameter buffers from a pool
- `SpatialHashBroadDetector` uses the new `ParallelScan` class for both shape-cell-offset and cell-offset prefix sums, restoring multi-level support
- Remove the now-unused `gpu_scan_params`, `gpu_scan_elem_count`, `gpu_scan_block_sums` buffers and inline scan dispatch code from `SpatialHashBroadDetector::Impl`
- **BREAKING**: The existing `parallel_scan.comp` at `engine/Physics/shader/solver/XPBDSolver/parallel_scan.comp` is superseded; the XPBDSolver (if it uses it) must migrate to the new class

## Capabilities

### New Capabilities

- `gpu-parallel-scan`: A reusable GPU parallel prefix-sum algorithm encapsulating compute stage, per-pass parameter management, scratch buffer allocation, and multi-level dispatch orchestration. Consumers import it once and call a single `AddPasses` method per scan operation.

### Modified Capabilities

None — existing spec requirements are unchanged. The BroadDetector's internal implementation changes but the contract (output buffers, public API) remains identical.

## Impact

- **New files**: `engine/Physics/gpu_algorithm/ParallelScan.h`, `engine/Physics/gpu_algorithm/ParallelScan.cpp`, `engine/Physics/shader/algorithm/parallel_scan.comp`
- **Modified files**: `engine/Physics/Collision/SpatialHashBroadDetector.h`, `engine/Physics/Collision/SpatialHashBroadDetector.cpp` (remove inline scan code, consume `ParallelScan`)
- **CMake**: Add new shader source to the physics GLSL→SPIR-V build pipeline; add new `.cpp` to the physics library target
- **Shader build**: New SPIR-V output at `build/engine/Physics/spirv/algorithm/parallel_scan.comp.spv`
- **Superseded**: `engine/Physics/shader/solver/XPBDSolver/parallel_scan.comp` may be removed if XPBDSolver no longer references it
