# gpu-parallel-scan

## Purpose

Define the contract for a reusable GPU parallel exclusive prefix-sum algorithm (`ParallelScan` class and associated compute shader). Consumers obtain a self-contained scan executor, pass input/output buffers, and the class handles single- or multi-level dispatch orchestration with correct render-graph buffer dependency declarations.

## Requirements

### Requirement: ParallelScan class construction

The `ParallelScan` class SHALL be constructible with `(Rhi::DeviceContext &device_context)` alone. Shader loading, compute-pipeline acquisition and block-sums allocation SHALL be deferred until the first `Record` call. The class SHALL own its block-sums storage: the recursion writes one sum per block per level into a buffer the instance allocates, grows geometrically and reuses across calls, so a caller SHALL NOT size, allocate or bind any scratch.

The block-sums storage SHALL be sized for the largest element count the instance has been recorded against, SHALL grow when a call exceeds what it holds, SHALL NOT shrink when a later call is smaller, and SHALL NOT be reallocated by a call that fits. Its size SHALL account for every recursion level. Because the storage is shared by every call, the caller SHALL record a barrier between two `Record` calls on one instance in the same command buffer, and the class SHALL state that obligation in its interface documentation.

#### Scenario: Construction with valid parameters
- **WHEN** `ParallelScan` is constructed with a device context
- **THEN** no GPU resources are allocated and no shaders are loaded
- **AND** the internal compute pipeline and the block-sums storage are acquired on the first `Record` call
- **AND** no exceptions are thrown

#### Scenario: Construction rejects zero max_elem_count
- **WHEN** the element count is no longer a construction parameter
- **THEN** the former construction-time rejection is gone: `Record` accepts any element count, and a zero element count records nothing
- **AND** the storage is grown by the calls themselves rather than validated against a declared maximum

### Requirement: Separate input and output buffer bindings

The scan shader SHALL have separate bindings for `InputData` (readonly) and `OutputData` (write). The `ParallelScan` class SHALL accept distinct `ComputeBuffer&` references for input and output. The caller MAY pass the same buffer for both to achieve in-place scan; the class treats them as independent, and a caller doing so SHALL ensure the preceding writes to that buffer are visible to the dispatch.

#### Scenario: Scan writes to different output buffer
- **WHEN** `Record` is called with `input_buf` and `output_buf` referencing different `ComputeBuffer` objects
- **THEN** the shader reads from `input_buf` and writes to `output_buf`
- **AND** the input buffer content is not modified

#### Scenario: Scan writes to same buffer (in-place)
- **WHEN** `Record` is called with `input_buf` and `output_buf` referencing the same `ComputeBuffer`
- **THEN** the shader reads and writes the same buffer (in-place exclusive scan)
- **AND** the caller has recorded the barrier that makes the buffer's preceding writes visible

### Requirement: Shader file location and structure

The scan compute shader SHALL reside at `engine/Physics/shader/algorithm/parallel_scan.comp`. The shader SHALL implement the Blelloch work-efficient exclusive scan algorithm with workgroup size 256, processing 512 elements per workgroup (two loads per thread). The shader SHALL support three modes via the `ScanParams.mode` field: 0 = scan only, 1 = scan blocks + write per-block sums to `BlockSums`, 2 = add `BlockSums` values back to output.

#### Scenario: Shader builds to SPIR-V
- **WHEN** CMake is configured for the physics target
- **THEN** `glslangValidator` compiles `engine/Physics/shader/algorithm/parallel_scan.comp` to SPIR-V
- **AND** the output is placed at `build/engine/Physics/spirv/algorithm/parallel_scan.comp.spv`

### Requirement: Class location independence

The `ParallelScan` class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. It SHALL only depend on `Rhi::DeviceContext`, `ComputeBuffer`, and the RHI compute dispatch interface, plus `vk::CommandBuffer`.

#### Scenario: ParallelScan has no broad-phase dependency
- **WHEN** `ParallelScan.h` is compiled
- **THEN** it does NOT include any headers from `engine/Physics/Collision/`
- **AND** it does NOT include any headers from `engine/Physics/Solver/`

### Requirement: BroadDetector migration

`SpatialHashBroadDetector` SHALL use `ParallelScan` for both shape-cell-offset and cell-offset prefix-sum computations. The detector SHALL create a single `ParallelScan` instance, use it for both scans, and record the barrier each scan needs; the instance owns the block-sums storage both scans reuse. The inline scan dispatch code SHALL NOT be reintroduced, and the detector SHALL NOT own a scan-parameter, scan-element-count or block-sums buffer.

#### Scenario: BroadDetector shape-cell-offset scan uses ParallelScan
- **WHEN** the spatial-hash path is recorded with `shape_count = 2000`
- **THEN** the shape-cell-offset prefix sum is performed by `ParallelScan::Record`
- **AND** three levels of scan dispatches are recorded for the multi-level scan
- **AND** `gpu_cell_offsets` receives the correct exclusive scan of `gpu_shape_cell_count`

#### Scenario: BroadDetector cell-offset scan uses ParallelScan
- **WHEN** the spatial-hash path is recorded
- **THEN** the cell-offset prefix sum is performed by `ParallelScan::Record`
- **AND** `gpu_cell_offsets` receives the correct exclusive scan of `gpu_cell_histogram`
