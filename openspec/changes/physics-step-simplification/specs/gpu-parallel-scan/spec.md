# gpu-parallel-scan

## MODIFIED Requirements

### Requirement: ParallelScan class construction

The `ParallelScan` class SHALL be constructible with `(Rhi::DeviceContext &device_context, uint32_t max_elem_count)`. Shader loading and compute-pipeline acquisition SHALL be deferred until the first `Record` call. The class SHALL NOT internally allocate a block-sums scratch buffer; the scratch buffer is caller-provided to `Record` and sized using the static helper `GetRequiredBlockSumsBytes(max_elem_count)`.

#### Scenario: Construction with valid parameters
- **WHEN** `ParallelScan` is constructed with a device context and `max_elem_count = 2000`
- **THEN** the internal compute pipeline is lazily acquired on first `Record` call
- **AND** `GetRequiredBlockSumsBytes(2000)` returns at least `ceil(2000 / 512) * sizeof(uint32_t)` bytes
- **AND** no exceptions are thrown

#### Scenario: Construction rejects zero max_elem_count
- **WHEN** `ParallelScan` is constructed with `max_elem_count = 0`
- **THEN** a `std::invalid_argument` exception is thrown (or the value is clamped to 1)

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

### Requirement: Class location independence

The `ParallelScan` class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. It SHALL only depend on `Rhi::DeviceContext`, `ComputeBuffer`, and the RHI compute dispatch interface, plus `vk::CommandBuffer`.

#### Scenario: ParallelScan has no broad-phase dependency
- **WHEN** `ParallelScan.h` is compiled
- **THEN** it does NOT include any headers from `engine/Physics/Collision/`
- **AND** it does NOT include any headers from `engine/Physics/Solver/`

### Requirement: BroadDetector migration

`SpatialHashBroadDetector` SHALL use `ParallelScan` for both shape-cell-offset and cell-offset prefix-sum computations. The detector SHALL create a single block-sums scratch buffer (sized via `ParallelScan::GetRequiredBlockSumsBytes`), reuse that same buffer for both scans, and record the barrier each scan needs. The inline scan dispatch code SHALL NOT be reintroduced, and the detector SHALL NOT own a separate scan-parameter or scan-element-count buffer.

#### Scenario: BroadDetector shape-cell-offset scan uses ParallelScan
- **WHEN** the spatial-hash path is recorded with `shape_count = 2000`
- **THEN** the shape-cell-offset prefix sum is performed by `ParallelScan::Record`
- **AND** three levels of scan dispatches are recorded for the multi-level scan
- **AND** `gpu_cell_offsets` receives the correct exclusive scan of `gpu_shape_cell_count`

#### Scenario: BroadDetector cell-offset scan uses ParallelScan
- **WHEN** the spatial-hash path is recorded
- **THEN** the cell-offset prefix sum is performed by `ParallelScan::Record`
- **AND** `gpu_cell_offsets` receives the correct exclusive scan of `gpu_cell_histogram`

## REMOVED Requirements

### Requirement: Single-call AddPasses API

**Reason**: The requirement describes `ParallelScan::AddPasses(RenderGraphBuilder&, RGBufferHandle, ...)`. The physics pipeline no longer uses `RenderGraph` at all — the archived `remove-rendergraph-from-physics-gpu` change replaced graph building with direct command-buffer recording — so the method, its builder parameter, and its resource handles do not exist. The capability's own level-counting and scratch-sizing contract survives in *ParallelScan class construction*.

**Migration**: Callers record a scan with `Record(vk::CommandBuffer cb, ...)`, passing the input buffer, the output buffer, the caller-owned block-sums scratch, and the element count; the class records its levels and their barriers directly to the command buffer. The `elem_count <= max_elem_count` precondition is still enforced by the call.

### Requirement: Per-pass parameter buffers

**Reason**: Each pass used to bind its own dedicated 12-byte host-visible parameter buffer so that a later pass could not overwrite an earlier pass's parameters before execution. Per-dispatch parameters are now recorded as push constants immediately before each dispatch, which removes both the buffer and the race the requirement existed to avoid (see `physics-push-constants`, *Parameter pools are removed*).

**Migration**: The `{mode, block_offset, elem_count}` triple travels in the shader's push-constant block, recorded once per dispatch. No parameter buffer is allocated, written from the CPU, or bound.

### Requirement: Correct render-graph buffer dependencies

**Reason**: The requirement obliges `AddPasses` to declare `UseBuffer` accesses so a `RenderGraph` can insert barriers. With `RenderGraph` removed from the physics pipeline there is no graph to declare to, and no handle to declare.

**Migration**: The caller records the required `vk::MemoryBarrier2` between levels. Barrier placement in the physics pipeline is explicit and belongs to the recording code, not to a graph.
