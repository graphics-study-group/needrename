# gpu-radix-sort

## Purpose

Define the contract for a reusable GPU 8-bit LSD radix sort algorithm (`RadixSort` class and associated compute shaders) that sorts `uvec2` pairs by `(primary, secondary)` key. Consumers obtain a self-contained sort executor, pass input/output pair buffers and scratch buffer, and the class handles the 8-pass dispatch orchestration with correct render-graph buffer dependency declarations.

## ADDED Requirements

### Requirement: RadixSort class construction

The `RadixSort` class SHALL be constructible with a `RenderSystem&` and a `uint32_t max_elem_count`. The constructor SHALL NOT allocate any GPU resources. Shader loading and `ComputeStage` instantiation SHALL be deferred until the first `AddPasses` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. Dependencies SHALL be limited to `RenderSystem`, `ComputeBuffer`, `ComputeStage`, `ComputeResourceBinding`, `RenderGraphBuilder`, and related render infrastructure.

#### Scenario: Construction with valid parameters

- **WHEN** `RadixSort` is constructed with `RenderSystem& rs` and `max_elem_count = 10000`
- **THEN** no GPU resources are allocated
- **AND** no shaders are loaded
- **AND** no exceptions are thrown

#### Scenario: Construction rejects zero max_elem_count

- **WHEN** `RadixSort` is constructed with `max_elem_count = 0`
- **THEN** a `std::invalid_argument` exception is thrown

### Requirement: Static sizing helpers

The `RadixSort` class SHALL expose static methods for callers to determine required scratch buffer sizes before constructing the instance:

- `GetRequiredScratchBytes()` SHALL return `256 * sizeof(uint32_t)` (1 KB) — the histogram/atomic-counter buffer size.
- `GetRequiredTempPairsBytes(uint32_t max_elem_count)` SHALL return `static_cast<size_t>(max_elem_count) * sizeof(glm::uvec2)` — the ping-pong pairs buffer size.

#### Scenario: Scratch buffer sizing is constant

- **WHEN** `RadixSort::GetRequiredScratchBytes()` is called
- **THEN** the return value is always 1024 bytes (256 × 4)

#### Scenario: Temp pairs buffer scales with element count

- **WHEN** `RadixSort::GetRequiredTempPairsBytes(5000)` is called
- **THEN** the return value is `5000 * 8 = 40000` bytes

### Requirement: Single-call AddPasses API

`RadixSort` SHALL expose an `AddPasses` method with the following signature:

```cpp
void AddPasses(
    RenderGraphBuilder &builder,
    RGBufferHandle pairs_handle_a,    // ping pairs
    RGBufferHandle pairs_handle_b,    // pong pairs (temp)
    ComputeBuffer &pairs_buf_a,
    ComputeBuffer &pairs_buf_b,
    RGBufferHandle scratch_handle,    // 256-uint histogram
    ComputeBuffer &scratch_buf,
    uint32_t elem_capacity,           // buffer capacity in pairs (for dispatch sizing)
    RGBufferHandle pair_count_handle, // RG handle for the actual pair count buffer
    ComputeBuffer &pair_count_buf,    // GPU-side uint, written by upstream passes
    uint32_t max_shape_count
);
```

The method SHALL sort `uvec2` pairs in-place (final sorted result in `pairs_buf_a` after 8 ping-pong passes). The sort order SHALL be by `.x` (primary key) then `.y` (secondary key), ascending.

**Dispatch sizing**: The method SHALL dispatch workgroups based on `elem_capacity` (the buffer capacity, typically `max_pairs`). The actual number of valid pairs is read at GPU execution time from `pair_count_buf` — each thread SHALL return immediately if `idx >= pair_count.count`. This is required because at RenderGraph build time the GPU has not yet executed the upstream pair-generation passes; reading `pair_count` via `GetVMAddress()` would return stale/garbage data.

If `elem_capacity == 0`, the method SHALL return immediately without adding any passes.

If `max_shape_count > 2^20` (1,048,576), the method SHALL throw `std::runtime_error`.

#### Scenario: Sorted output ends up in pairs_buf_a

- **WHEN** `AddPasses` is called with `elem_capacity = 10000` and the GPU-side `pair_count = 100`
- **THEN** the shader dispatches `ceil(10000 / 64)` workgroups, but only processes the first 100 elements
- **AND** after execution, `pairs_buf_a` contains the sorted pairs in its first 100 slots

#### Scenario: Elements are sorted by (a, b) ascending

- **WHEN** input pairs are `[(5,2), (2,3), (2,1)]` and sorted
- **THEN** the sorted output is `[(2,1), (2,3), (5,2)]`

#### Scenario: Identical pairs are adjacent after sort

- **WHEN** input pairs are `[(3,7), (1,4), (3,7)]` and sorted
- **THEN** the sorted output contains `(3,7)` and `(3,7)` adjacent (exact relative order between identical pairs is not guaranteed)

#### Scenario: Empty capacity produces no passes

- **WHEN** `AddPasses` is called with `elem_capacity = 0`
- **THEN** no compute passes are added to the builder
- **AND** the method returns immediately

#### Scenario: Zero pairs at execution time

- **WHEN** the GPU-side `pair_count` buffer contains 0 at execution time
- **THEN** all dispatched threads return immediately (no sorting work done)
- **AND** no out-of-bounds access occurs

#### Scenario: Shape count exceeds limit throws

- **WHEN** `AddPasses` is called with `max_shape_count = 1048577` (> 2^20)
- **THEN** a `std::runtime_error` is thrown

### Requirement: 8-bit LSD radix sort algorithm

The sort SHALL use 8-bit Least Significant Digit (LSD) radix sort, processing pairs in 8 passes: passes 0-3 sort by `.y` (secondary key), passes 4-7 sort by `.x` (primary key). Each pass processes one byte (8 bits) of the selected word.

Each radix pass SHALL consist of three sub-steps:
1. **Histogram** (`radix_histogram.comp`): Count elements per digit (0-255) via `atomicAdd` into a 256-entry histogram buffer. The histogram SHALL be cleared to zero before this step.
2. **Prefix sum** (`radix_prefix_sum_256.comp`): Perform exclusive prefix sum over the 256-entry histogram in-place using shared-memory Blelloch scan (single workgroup of 256 threads).
3. **Scatter** (`radix_scatter.comp`): Reorder elements by digit. For each input element, extract the digit, atomically get write position from the prefix-summed histogram (`atomicAdd(histogram[digit], 1)`), and write the pair to that position in the output buffer.

Steps 1-2 SHALL read from the current input buffer; step 3 SHALL write to the current output buffer. Input and output buffers SHALL be swapped (ping-pong) between passes.

#### Scenario: Histogram pass counts per digit

- **WHEN** 100 pairs have sort keys with byte values in the range [0, 255]
- **THEN** after the histogram pass, `histogram[d]` equals the number of pairs whose current sort byte is `d`
- **AND** the sum of all 256 histogram entries equals `elem_count`

#### Scenario: Prefix sum produces exclusive scan

- **WHEN** histogram contains `[3, 2, 0, ..., 0]` (3 elements with digit 0, 2 with digit 1)
- **THEN** after prefix sum, histogram contains `[0, 3, 5, 5, ..., 5]` (exclusive scan)

#### Scenario: Scatter reorders by digit

- **WHEN** after prefix sum, `histogram[5] = 10` (10 elements with digit < 5)
- **AND** an input element has sort byte = 5
- **THEN** the scatter pass writes that element to position ≥ 10 and < 10+count[5] in the output buffer

#### Scenario: Ping-pong swaps buffers between passes

- **WHEN** pass 0 writes sorted output to `pairs_buf_b`
- **THEN** pass 1 reads from `pairs_buf_b` and writes to `pairs_buf_a`
- **AND** after an even number of total passes (8), the final result is in `pairs_buf_a`

### Requirement: Per-pass parameter buffers

Each compute dispatch added by `RadixSort::AddPasses` SHALL bind its own dedicated host-visible parameter buffer. No two dispatches SHALL share the same parameter buffer. Each parameter buffer SHALL contain:

```glsl
layout(set = 0, binding = N) readonly buffer RadixParams {
    uint byte_shift;     // 0, 8, 16, or 24
    uint word_select;    // 0 = pair.y (secondary), 1 = pair.x (primary)
    uint elem_capacity;  // buffer capacity (for dispatch sizing only)
    uint _pad;
} radix_params;
```

The `elem_capacity` field SHALL be set to the buffer capacity (typically `max_pairs`) and SHALL be used only for CPU-side dispatch workgroup count calculation. It SHALL NOT be used as the logical element count in shaders.

#### Scenario: Each of 24 dispatches has its own param buffer

- **WHEN** `AddPasses` adds 24 compute dispatches (8 passes × 3 sub-steps)
- **THEN** 24 distinct parameter buffers are allocated from the internal pool
- **AND** each buffer's `byte_shift` and `word_select` match its pass and sub-step

### Requirement: PairCount buffer binding for GPU-side element count

The `radix_histogram.comp` and `radix_scatter.comp` shaders SHALL each bind a `PairCount` readonly buffer that provides the actual number of valid pairs at GPU execution time:

```glsl
layout(set = 0, binding = N) readonly buffer PairCount {
    uint count;
} pair_count;
```

Each thread SHALL check `idx >= pair_count.count` and return immediately if true. This SHALL be the ONLY element-count guard in the shader; the `elem_capacity` field in `RadixParams` is used only for CPU-side dispatch sizing.

The `pair_count_buf` SHALL be a GPU buffer written by upstream passes (pair generation) in the same frame. At RenderGraph build time this buffer's value is NOT valid — the shader reads it at GPU execution time after the upstream passes have completed and the RG barrier has been satisfied.

#### Scenario: Threads beyond actual count are skipped

- **WHEN** `elem_capacity = 10000` (dispatched 157 workgroups)
- **AND** the GPU-side `pair_count.count = 50`
- **THEN** threads with `idx >= 50` return immediately without reading from `PairsIn`
- **AND** threads with `idx < 50` process normally

#### Scenario: PairCount handle is declared for RG barrier tracking

- **WHEN** a histogram or scatter pass is added to the render graph
- **THEN** `UseBuffer(pair_count_handle, RR)` SHALL be declared on the pass
- **AND** the render graph SHALL insert a barrier between the upstream pair-generation write and this pass's read

### Requirement: Scratch buffer reuse across passes

The 256-uint scratch buffer SHALL be cleared to zero before each histogram pass (using the existing `memset_uint.comp` shader). After prefix sum, the scratch buffer SHALL contain exclusive prefix sums. During scatter, the scratch buffer SHALL be modified via `atomicAdd` (serving as atomic counters). The next radix pass SHALL clear and rebuild the scratch buffer.

#### Scenario: Scratch buffer corrupted after scatter

- **WHEN** a scatter pass completes
- **THEN** `scratch[digit]` contains the END position (not start) for each digit
- **AND** the next radix pass clears scratch to zero before its histogram step

### Requirement: Shader file locations

The radix sort compute shaders SHALL reside at:

| Shader | Path |
|--------|------|
| `radix_histogram.comp` | `engine/Physics/shader/algorithm/radix_histogram.comp` |
| `radix_prefix_sum_256.comp` | `engine/Physics/shader/algorithm/radix_prefix_sum_256.comp` |
| `radix_scatter.comp` | `engine/Physics/shader/algorithm/radix_scatter.comp` |

All three SHALL compile to SPIR-V via `glslangValidator` and be placed in the build output at `build/engine/Physics/spirv/algorithm/`.

#### Scenario: Shaders compile to SPIR-V

- **WHEN** CMake is configured for the physics target
- **THEN** all three radix sort shader files compile to `.spv` files in the spirv output directory
