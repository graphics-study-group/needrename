# gpu-parallel-scan

## Purpose

Govern the reusable GPU parallel prefix sum (exclusive scan) compute shader and its `ParallelScan` C++ executor class, used internally by `SpatialHashBroadDetector` for offset computation.

## ADDED Requirements

### Requirement: Blelloch work-efficient exclusive scan shader

The system SHALL provide a compute shader `parallel_scan.comp` under `engine/Physics/shader/algorithm/` that computes an exclusive prefix sum of a `uint` input buffer into a `uint` output buffer using the Blelloch work-efficient scan algorithm.

For a single-workgroup scan (buffer size ≤ 512 elements), the shader SHALL perform upsweep (reduce) and downsweep (distribution) phases entirely in workgroup shared memory, using 256 threads with 2 loads per thread.

For larger buffers, the shader SHALL support multi-level scan via a `mode` parameter: partition the input into 512-element blocks, scan each block locally and write per-block sums to a scratch buffer, recursively scan the block sums, then add the scanned block sums back to each block's output using a separate `add_block_offset.comp` shader.

The output SHALL be exclusive: `output[0] = 0`, `output[i] = sum(input[0..i-1])` for `i > 0`.

#### Scenario: Small array scan

- **WHEN** input is `[3, 1, 7, 0, 4]` with 5 elements
- **THEN** the exclusive scan output is `[0, 3, 4, 11, 11]`
- **AND** the shader uses only shared memory (single workgroup, mode=0)

#### Scenario: Large array scan

- **WHEN** input is a buffer of 100,000 `uint` values
- **THEN** the exclusive scan produces correct results for all elements
- **AND** the shader uses the multi-level approach (mode=1 followed by recursive scan and offset addition)

#### Scenario: Scan of zero-buffer

- **WHEN** all input elements are 0
- **THEN** all output elements are 0

### Requirement: ParallelScan executor class

The C++ side SHALL provide a `ParallelScan` class under `engine/Physics/gpu_algorithm/ParallelScan.h` that encapsulates the scan compute pipeline, per-pass parameter management, and multi-level dispatch orchestration. The class SHALL own the compute stages for `parallel_scan.comp` (modes 0 and 1) and `add_block_offset.comp`.

The caller SHALL provide input/output data buffers and a block-sums scratch buffer (sized via `ParallelScan::GetRequiredBlockSumsBytes()`). The class SHALL determine whether single-level or multi-level dispatch is needed based on the input element count.

#### Scenario: Single-level dispatch for small count

- **WHEN** `ParallelScan::AddPasses(builder, input_h, output_h, ..., 256)` is called
- **THEN** one compute pass is dispatched (mode=0)

#### Scenario: Multi-level dispatch for large count

- **WHEN** `ParallelScan::AddPasses(builder, input_h, output_h, ..., 100000)` is called
- **THEN** multiple compute passes are dispatched across potentially multiple recursion levels

### Requirement: In-place scan support

The shader SHALL support in-place operation where the input and output buffers are the same. The caller passes the same `ComputeBuffer` reference and the same render graph handle for both.

#### Scenario: In-place scan preserves correctness

- **WHEN** input buffer `[2, 3, 1]` is scanned in-place
- **THEN** the buffer contains `[0, 2, 5]` after the scan
