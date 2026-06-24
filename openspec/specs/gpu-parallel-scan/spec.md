# gpu-parallel-scan

## Purpose

Define the contract for a reusable GPU parallel exclusive prefix-sum algorithm (`ParallelScan` class and associated compute shader). Consumers obtain a self-contained scan executor, pass input/output buffers, and the class handles single- or multi-level dispatch orchestration with correct render-graph buffer dependency declarations.

## Requirements

### Requirement: ParallelScan class construction

The `ParallelScan` class SHALL be constructible with a `RenderSystem&` and a `uint32_t max_elem_count`. The constructor SHALL load and instantiate the scan compute shader from `engine/Physics/shader/algorithm/parallel_scan.comp` (lazily, on first `AddPasses` call). The class SHALL NOT internally allocate a block-sums scratch buffer; the scratch buffer is caller-provided via `AddPasses` and sized using the static helper `GetRequiredBlockSumsBytes(max_elem_count)`.

#### Scenario: Construction with valid parameters
- **WHEN** `ParallelScan` is constructed with `RenderSystem& rs` and `max_elem_count = 2000`
- **THEN** the internal compute stage is lazily initialized on first `AddPasses` call
- **AND** `GetRequiredBlockSumsBytes(2000)` returns at least `ceil(2000 / 512) * sizeof(uint32_t)` bytes
- **AND** no exceptions are thrown

#### Scenario: Construction rejects zero max_elem_count
- **WHEN** `ParallelScan` is constructed with `max_elem_count = 0`
- **THEN** a `std::invalid_argument` exception is thrown (or the value is clamped to 1)

### Requirement: Single-call AddPasses API

`ParallelScan` SHALL expose an `AddPasses` method that accepts a `RenderGraphBuilder&`, an `RGBufferHandle` for the input data buffer, an `RGBufferHandle` for the output data buffer, a `ComputeBuffer&` for input data, a `ComputeBuffer&` for output data, an `RGBufferHandle` for the block-sums scratch buffer (pre-imported by the caller), a `ComputeBuffer&` for the block-sums scratch buffer, and a `uint32_t elem_count`. The method SHALL internally determine the number of dispatch levels and add all necessary passes to the builder. The method SHALL assert that `elem_count <= max_elem_count`. The scratch buffer and its handle SHALL be caller-provided so that the same handle can be reused across multiple `AddPasses` calls, ensuring correct render-graph dependency tracking.

#### Scenario: Single-level scan (elem_count ≤ 512)
- **WHEN** `AddPasses` is called with `elem_count = 256`
- **THEN** exactly one compute pass is added to the builder
- **AND** the pass uses mode=0 (scan only) with one workgroup

#### Scenario: Multi-level scan (elem_count > 512)
- **WHEN** `AddPasses` is called with `elem_count = 2000`
- **THEN** exactly three compute passes are added to the builder (level 1: mode=1 scan blocks + write sums; level 2: mode=0 scan block sums; level 3: mode=2 add offsets back)
- **AND** passes are added in sequential order

#### Scenario: Elem count exceeds max
- **WHEN** `AddPasses` is called with `elem_count > max_elem_count`
- **THEN** an assertion fires (debug) or a `std::runtime_error` is thrown

### Requirement: Per-pass parameter buffers

Each compute pass added by `ParallelScan::AddPasses` SHALL bind its own dedicated host-visible parameter buffer (12 bytes: `mode`, `block_offset`, `elem_count` as three `uint32_t` values). No two passes SHALL share the same parameter buffer.

#### Scenario: Three-pass scan has three distinct parameter buffers
- **WHEN** a multi-level scan adds three passes
- **THEN** each pass binds a different `ComputeBuffer` for the `ScanParams` binding
- **AND** each parameter buffer was written with the correct `{mode, block_offset, elem_count}` for its pass

### Requirement: Separate input and output buffer bindings

The scan shader SHALL have separate bindings for `InputData` (readonly) and `OutputData` (write). The `ParallelScan` class SHALL accept distinct `ComputeBuffer&` references for input and output. The caller MAY pass the same buffer for both to achieve in-place scan; the class treats them as independent.

#### Scenario: Scan writes to different output buffer
- **WHEN** `AddPasses` is called with `input_buf` and `output_buf` referencing different `ComputeBuffer` objects
- **THEN** the shader reads from `input_buf` and writes to `output_buf`
- **AND** the input buffer content is not modified

#### Scenario: Scan writes to same buffer (in-place)
- **WHEN** `AddPasses` is called with `input_buf` and `output_buf` referencing the same `ComputeBuffer`
- **THEN** the shader reads and writes the same buffer (in-place exclusive scan)
- **AND** the render-graph handle for that buffer is declared with `ShaderRandomRead | ShaderRandomWrite`

### Requirement: Correct render-graph buffer dependencies

`AddPasses` SHALL declare `UseBuffer` on every `RGBufferHandle` that each pass accesses, with the correct read/write access flags. The block-sums scratch buffer SHALL be imported once and its handle reused across all passes that access it.

#### Scenario: Multi-level scan declares proper dependencies
- **WHEN** a three-level scan is added
- **THEN** level 1 declares `UseBuffer(data_handle, RR|RW)` and `UseBuffer(block_sums_handle, WW)`
- **AND** level 2 declares `UseBuffer(block_sums_handle, RR|RW)`
- **AND** level 3 declares `UseBuffer(data_handle, RR|RW)` and `UseBuffer(block_sums_handle, RR)`
- **AND** the render graph can correctly insert barriers between levels

### Requirement: Shader file location and structure

The scan compute shader SHALL reside at `engine/Physics/shader/algorithm/parallel_scan.comp`. The shader SHALL implement the Blelloch work-efficient exclusive scan algorithm with workgroup size 256, processing 512 elements per workgroup (two loads per thread). The shader SHALL support three modes via the `ScanParams.mode` field: 0 = scan only, 1 = scan blocks + write per-block sums to `BlockSums`, 2 = add `BlockSums` values back to output.

#### Scenario: Shader builds to SPIR-V
- **WHEN** CMake is configured for the physics target
- **THEN** `glslangValidator` compiles `engine/Physics/shader/algorithm/parallel_scan.comp` to SPIR-V
- **AND** the output is placed at `build/engine/Physics/spirv/algorithm/parallel_scan.comp.spv`

### Requirement: Class location independence

The `ParallelScan` class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. It SHALL only depend on `RenderSystem`, `ComputeBuffer`, `ComputeStage`, `ComputeResourceBinding`, `RenderGraphBuilder`, and related render infrastructure.

#### Scenario: ParallelScan has no broad-phase dependency
- **WHEN** `ParallelScan.h` is compiled
- **THEN** it does NOT include any headers from `engine/Physics/Collision/`
- **AND** it does NOT include any headers from `engine/Physics/Solver/`

### Requirement: BroadDetector migration

`SpatialHashBroadDetector` SHALL use `ParallelScan` for both shape-cell-offset and cell-offset prefix-sum computations. The detector SHALL create a single block-sums scratch buffer (sized via `ParallelScan::GetRequiredBlockSumsBytes`), import it into the render graph once, and pass the same buffer and handle to both `AddPasses` calls. The inline scan dispatch code and the commented-out `DispatchParallelScan` SHALL be removed. The old detector-owned scan buffers (`gpu_scan_params`, `gpu_scan_elem_count`, `gpu_scan_block_sums`) SHALL be removed from `Impl`.

#### Scenario: BroadDetector shape-cell-offset scan uses ParallelScan
- **WHEN** `AddDetectPasses` is called with `shape_count = 2000`
- **THEN** the shape-cell-offset prefix sum is performed via `ParallelScan::AddPasses`
- **AND** three passes (level 1/2/3) are added for the multi-level scan
- **AND** `gpu_cell_offsets` receives the correct exclusive scan of `gpu_shape_cell_count`

#### Scenario: BroadDetector cell-offset scan uses ParallelScan
- **WHEN** `AddDetectPasses` is called and the spatial-hash path is taken
- **THEN** the cell-offset prefix sum is performed via `ParallelScan::AddPasses`
- **AND** `gpu_cell_offsets` receives the correct exclusive scan of `gpu_cell_histogram`
