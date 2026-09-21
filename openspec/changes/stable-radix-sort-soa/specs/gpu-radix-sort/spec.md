# gpu-radix-sort

## MODIFIED Requirements

### Requirement: RadixSort class construction

The `RadixSort` class SHALL be constructible with a `Rhi::DeviceContext &` alone. Element geometry SHALL NOT be a construction-time parameter and the instance SHALL hold no geometry state. The constructor SHALL NOT allocate any GPU resources. Shader loading and `ComputeStage` instantiation SHALL be deferred until the first `Record` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types. Dependencies SHALL be limited to `Rhi::DeviceContext`, `ComputeBuffer`, `ComputeStage`, `ComputeResourceBinding`, `ParallelScan` and `vk::CommandBuffer`.

Because the element capacity and the key bound are supplied per call, one instance SHALL be reusable for any capacity and any bound, including several different ones within a single frame, and SHALL NOT require rebuilding when the caller's geometry changes.

The class SHALL own an internal `ParallelScan` instance for its prefix-sum step and SHALL rebuild it internally when a call's element capacity exceeds the capacity it was built for. That instance SHALL NOT appear in the class's interface, and the class SHALL NOT allocate any buffer: every buffer it binds SHALL be caller-provided.

#### Scenario: Construction with valid parameters

- **WHEN** `RadixSort` is constructed with a device context
- **THEN** no GPU resources are allocated and no shaders are loaded
- **AND** `IsInitialized()` returns false

#### Scenario: Construction rejects zero max_elem_count

The element count is no longer a construction parameter, so the former construction-time rejection is replaced by per-call behaviour: a zero capacity is a no-op at `Record`, while a capacity whose implied byte size exceeds the bound buffers is rejected there.

- **WHEN** `Record` is called with a capacity of zero
- **THEN** no dispatch is recorded and no exception is thrown

#### Scenario: One instance serves several capacities

- **WHEN** the same instance records two sorts with different element capacities in one frame
- **THEN** each call dispatches for its own capacity
- **AND** no rebuild is required at the call site

### Requirement: Static sizing helpers

The `RadixSort` class SHALL expose static methods for callers to size the buffers they provide, expressed in terms of the largest element capacity the caller will ever pass:

- `GetRequiredScratchBytes(uint32_t max_elem_capacity)` SHALL return the total size of the single scratch buffer the sort needs: `256 * ceil(max_elem_capacity / 256)` `uint`s for the transposed per-block digit histogram, plus `ParallelScan::GetRequiredBlockSumsBytes(256 * ceil(max_elem_capacity / 256))` for the scan's block sums. The class SHALL partition that buffer internally and SHALL NOT expose the partition.
- `GetRequiredTempBytes(uint32_t max_elem_capacity)` SHALL return `max_elem_capacity * sizeof(uint32_t)` — the size of **each** ping-pong temporary array (one for keys, and one more for the payload array when the caller supplies one).

Both helpers SHALL be callable before any instance exists.

#### Scenario: Scratch buffer sizing is constant

The scratch size is no longer a constant: it scales with the capacity the caller declares, because it holds the transposed per-block histogram and the scan's block sums.

- **WHEN** `GetRequiredScratchBytes(2560)` is called
- **THEN** the return value is `2560 * 4 + ParallelScan::GetRequiredBlockSumsBytes(2560)` bytes

#### Scenario: Temp pairs buffer scales with element count

- **WHEN** `GetRequiredTempBytes(5000)` is called
- **THEN** the return value is `5000 * 4 = 20000` bytes

### Requirement: 8-bit LSD radix sort algorithm

The sort SHALL be an 8-bit Least Significant Digit radix sort: one pass per significant byte of the key, least significant byte first, with the pass count derived from the caller's key bound (see *Stable ascending sort with a derived pass count*). Each pass SHALL write into the key array that is not its input, and consecutive passes SHALL alternate between the two caller-provided key arrays.

Each pass SHALL consist of exactly three sub-steps, with no clear pass:

1. **Per-block histogram** (`radix_block_histogram.comp`): each block of 256 elements builds a 256-bin histogram of its own elements' digits in shared memory and writes it **transposed** into the scratch buffer, so that `hist[digit * num_blocks + block]` holds the count of that digit in that block. Every cell of that array SHALL be written by exactly one invocation on every call, which is why no clear pass is required.
2. **Prefix sum** (the internal `ParallelScan`): an in-place exclusive scan over the whole `256 * num_blocks`-element transposed histogram. Because the digit is the outer dimension and the block the inner one, the scanned value at `digit * num_blocks + block` is exactly the sum of the digit's global base offset and the counts of that digit in all earlier blocks.
3. **Stable scatter** (`radix_scatter.comp`): each element's destination is the scanned offset for its `(digit, block)` pair plus its deterministic in-block rank (see *Stable per-block digit ranking*).

#### Scenario: Transposed histogram covers every cell

- **WHEN** a pass runs with `elem_capacity = 2310` (10 blocks of 256)
- **THEN** the scratch's transposed histogram region holds `256 * 10` entries
- **AND** each entry was written by exactly one invocation of the histogram sub-step

#### Scenario: The scan yields base offset plus earlier-block offset

- **WHEN** two blocks and two digits are present, with counts `hist[0][0]=2`, `hist[0][1]=1`, `hist[1][0]=3`, `hist[1][1]=0`
- **THEN** the exclusive scan of the transposed array `[2, 1, 3, 0]` is `[0, 2, 3, 6]`
- **AND** `scanned[1 * 2 + 0] = 3` is the base offset of digit 1
- **AND** `scanned[0 * 2 + 1] = 2` is digit 0's offset after block 0

#### Scenario: Histogram pass counts per digit

- **WHEN** 100 elements have digits spread over `[0, 255]`
- **THEN** after the histogram sub-step, the transposed array holds, for every `(digit, block)` pair, the number of that block's elements carrying that digit
- **AND** the sum over all 256 digits of a block's column equals that block's active element count

#### Scenario: Prefix sum produces exclusive scan

- **WHEN** the transposed histogram of a single block is `[3, 2, 0, ..., 0]` (3 elements with digit 0, 2 with digit 1)
- **THEN** after the scan it holds `[0, 3, 5, 5, ..., 5]` (exclusive scan)

#### Scenario: Scatter reorders by digit

- **WHEN** the scanned offset for digit 5 is 10 and an element carries digit 5
- **THEN** the scatter writes that element to a slot at or after 10 and before 10 plus digit 5's total count

#### Scenario: Ping-pong swaps buffers between passes

- **WHEN** a two-pass sort is recorded with `keys_a` and `keys_b`
- **THEN** the first pass reads `keys_a` and writes `keys_b`
- **AND** the second pass reads `keys_b` and writes `keys_a`

### Requirement: PairCount buffer binding for GPU-side element count

The `radix_block_histogram.comp` and `radix_scatter.comp` shaders SHALL each bind a `ElemCount` readonly buffer providing the actual number of valid elements at GPU execution time:

```glsl
layout(set = 0, binding = N) readonly buffer ElemCount {
    uint count;
} elem_count;
```

An invocation whose element index is at or beyond that count SHALL contribute no histogram count, no rank and no output write. Because the shaders contain `barrier()` calls, that SHALL be expressed as predication over the whole body and SHALL NOT be an early `return`: a non-uniform early return before a barrier is undefined behaviour, and in the histogram sub-step it would also leave that invocation's transposed cell unwritten. An inactive invocation SHALL contribute a sentinel digit that no real element can produce, so that the rank computation and the per-warp histograms agree on which invocations are active.

Dispatch workgroup count SHALL be calculated from `elem_capacity`, never from the GPU-side count. The count buffer SHALL be a GPU buffer written by an upstream pass in the same frame and SHALL be read at execution time.

#### Scenario: Threads beyond actual count are skipped

- **WHEN** `elem_capacity = 10000` and the GPU-side count is 50
- **THEN** invocations with an index at or beyond 50 write no histogram count, claim no rank and write no output
- **AND** the sorted prefix is identical to a sort of those 50 elements alone

#### Scenario: PairCount handle is declared for RG barrier tracking

No render graph exists any more, so there is no handle to declare: `Record` itself inserts the compute barrier that orders the upstream count write before the sort's first read of it.

- **WHEN** `Record` is called with a count buffer that an upstream pass writes in the same command buffer
- **THEN** a compute barrier separates that write from the sort's first read

#### Scenario: The histogram region is fully written even with a small count

- **WHEN** the count is smaller than the capacity and whole blocks lie beyond it
- **THEN** every cell of the transposed histogram is still written
- **AND** blocks beyond the count contribute zero counts

### Requirement: Shader file locations

The radix sort compute shaders SHALL reside at:

| Shader | Path |
|--------|------|
| `radix_block_histogram.comp` | `engine/Physics/shader/algorithm/radix_block_histogram.comp` |
| `radix_scatter.comp` | `engine/Physics/shader/algorithm/radix_scatter.comp` |

`radix_histogram.comp` and `radix_prefix_sum_256.comp` SHALL NOT exist: the per-block histogram replaces the global one, and the generic `ParallelScan` replaces the 256-element scan.

All of them SHALL compile to SPIR-V via `glslangValidator` and be placed in the build output at the physics SPIR-V root under `algorithm/`.

#### Scenario: Shaders compile to SPIR-V

- **WHEN** CMake is configured for the physics target
- **THEN** `radix_block_histogram.comp` and `radix_scatter.comp` compile to `.spv` files in the physics SPIR-V output directory under `algorithm/`

#### Scenario: Removed shaders are gone

- **WHEN** the physics shader pipeline is configured
- **THEN** no `radix_histogram.comp.spv` and no `radix_prefix_sum_256.comp.spv` is produced
- **AND** `radix_block_histogram.comp.spv` and `radix_scatter.comp.spv` are produced

## REMOVED Requirements

### Requirement: Single-call AddPasses API

**Reason**: The render-graph-based `AddPasses` API no longer exists — `RadixSort` records directly into a command buffer via `Record`, and its geometry travels per call. The requirement also described a `uvec2` record sorted by `(.x, .y)` in a fixed 8 passes, which this change replaces entirely.

**Migration**: Call `Record(cb, ...)` with the caller's key array, optional payload array, scratch buffer and count buffer, plus the element capacity and the key bound. Read the returned buffer references for the sorted result.

### Requirement: Per-pass parameter buffers

**Reason**: Per-dispatch host-visible parameter buffers were replaced by push constants (`RadixParamsPush`); the shaders declare `layout(push_constant)` blocks and no dispatch owns a parameter buffer.

**Migration**: None. The pass parameters (`byte_shift`, `word_select`) are pushed immediately before each dispatch.

### Requirement: Scratch buffer reuse across passes

**Reason**: The scratch buffer is no longer a 256-entry global histogram doubling as an atomic counter, so the "corrupted after scatter, cleared before the next pass" cycle is gone. The transposed per-block histogram is fully rewritten every pass, and no atomic claims a write position any more.

**Migration**: Size the scratch with `GetRequiredScratchBytes(max_elem_capacity)` and pass the same buffer to every `Record` call.

## ADDED Requirements

### Requirement: Key and payload record layout

`RadixSort` SHALL operate on a struct-of-arrays record: a `uint` key array plus an **optional** `uint` payload array of the same capacity. There SHALL be no packed `uvec2` record and no mode selector saying which word is the key: the key array is the ordering key and the payload array is opaque data that is permuted together with its key.

The payload array SHALL be optional, because a caller whose record *is* its key (for example a pair identity packed into one `uint`) has no payload to permute. When no payload is supplied, the sort SHALL NOT read or write a payload buffer; the shader's payload binding SHALL be satisfied with a placeholder and the payload store SHALL be predicated on a push-constant flag.

#### Scenario: Keys-only sort permutes nothing else

- **WHEN** `Record` is called without payload buffers
- **THEN** only the key arrays are read and written
- **AND** the output key array is the input key array in ascending order

#### Scenario: Payload rides along

- **WHEN** keys are `[5, 2, 2, 1]` and payloads are `[10, 20, 30, 40]`
- **THEN** the sorted output pairs are `(1,40)`, then `(2,20)` and `(2,30)` in that order, then `(5,10)`
- **AND** every payload value appears exactly once

### Requirement: Stable ascending sort with a derived pass count

The sort SHALL be **stable**: for any two elements `i < j` with equal keys, the element at `i` SHALL appear before the element at `j` in the output. Stability SHALL hold for every pass count, because LSD radix sort is correct only if each pass preserves the relative order established by the previous one.

The number of passes SHALL be derived from the caller's key bound and SHALL NOT be a fixed constant:

```
num_passes = ceil(bit_width(max_key_value) / 8)
```

A key bound that fits in one byte SHALL therefore produce exactly one pass, and a bound of zero or one SHALL produce no passes at all and leave the input untouched.

Because the pass count can be odd, the sorted result can end up in either of the two key arrays. `Record` SHALL therefore **return** references to the buffers holding the sorted result (the key array and, when present, the payload array) instead of requiring the caller to know the parity rule.

#### Scenario: Equal keys keep their input order

- **WHEN** keys are `[3, 1, 3, 1, 3, 1]` with payloads equal to their input indices, over a capacity that spans several blocks
- **THEN** the output keys are `[1, 1, 1, 3, 3, 3]`
- **AND** the payloads of the three `1`s are `1, 3, 5` in that order
- **AND** the payloads of the three `3`s are `0, 2, 4` in that order

#### Scenario: One pass for a one-byte key domain

- **WHEN** `Record` is called with `max_key_value = 200`
- **THEN** exactly one pass is recorded (three dispatches: histogram, scan, scatter)

#### Scenario: A small bound produces no passes

- **WHEN** `Record` is called with `max_key_value <= 1`
- **THEN** no dispatch is recorded
- **AND** the returned result buffers are the caller's input buffers

#### Scenario: The result buffer is returned, not assumed

- **WHEN** `Record` is called with an odd pass count
- **THEN** the returned key buffer reference is the one the last pass wrote
- **AND** the caller does not need to know the pass count to bind the sorted result

### Requirement: Key bound contract

`max_key_value` SHALL be a value at or above every key the caller will write, and the module SHALL NOT clamp, mask or otherwise repair a key that exceeds the bound: a clamp would merge out-of-range keys into the top bin, where their mutual order is arbitrary and cannot be recovered, producing a non-ascending output with no error.

The contract SHALL be stated as "every key is below `2^(8 * num_passes)`", so that a key exactly equal to `max_key_value` remains valid.

Verification SHALL be the caller's, on the host, because the module cannot know the caller's key domain: each call site SHALL assert that the keys it is about to produce fit its bound. `Record` SHALL itself reject a `max_key_value` of zero.

#### Scenario: Record rejects a zero bound

- **WHEN** `Record` is called with `max_key_value = 0`
- **THEN** a `std::invalid_argument` exception is thrown

#### Scenario: A key equal to the bound is still sorted

- **WHEN** `max_key_value` is 256 and one key equals 256
- **THEN** that key is sorted correctly, because 256 needs two bytes and the derived pass count covers them

### Requirement: Stable per-block digit ranking

`radix_scatter.comp` SHALL compute each element's destination without letting an atomic operation decide any element's order. For an element with digit `d` in block `b`, warp `w` and lane `l`, the destination SHALL be

```
pos = scanned[d * num_blocks + b] + warp_offset(d, b, w) + rank_in_warp(d, w, l)
```

where `rank_in_warp` is the number of earlier lanes of the same warp carrying the same digit, and `warp_offset` is the number of elements carrying that digit in earlier warps of the same block. Both terms SHALL be computed deterministically in shared memory from a snapshot of the block's digits (a per-warp histogram of counts plus a bounded comparison loop, or an equivalent deterministic construction). Shared-memory atomics MAY be used to build the per-warp **counts**, because counts are order-independent, but SHALL NOT be used to derive any rank.

The destination SHALL be a bijection onto `[0, count)`, so no two elements write the same slot.

#### Scenario: The rank decomposition is exact

- **WHEN** six elements with digits `[3, 1, 3, 1, 3, 1]` are sorted across two blocks
- **THEN** the destinations are `1→0`, `3→1`, `5→2`, `0→3`, `2→4`, `4→5`
- **AND** no destination is claimed twice

#### Scenario: A digit shared by a whole warp does not reorder it

- **WHEN** every element of a block carries the same digit
- **THEN** the block's elements keep their input order in the output

### Requirement: RadixSort Record API

`RadixSort` SHALL expose a single `Record` method that takes the command buffer, the caller's buffers, the element capacity and the key bound, and returns the buffers holding the sorted result:

```cpp
struct RadixSortOutput {
    Rhi::ComputeBuffer *keys;    // the array holding the sorted keys
    Rhi::ComputeBuffer *payload; // the array holding the permuted payloads, or nullptr
};

RadixSortOutput Record(
    vk::CommandBuffer cb,
    const RadixSortBuffers &buffers,  // key arrays a/b, optional payload arrays a/b, scratch, count
    uint32_t elem_capacity,
    uint32_t max_key_value
);
```

`Record` SHALL insert the barriers it needs internally, SHALL reject a capacity whose implied byte size exceeds the buffers bound for that call with a `std::runtime_error`, and SHALL record nothing (returning the input key array as the result) when the capacity is zero or the derived pass count is zero.

The caller SHALL NOT be required to know how many passes ran, which scratch region was used, or which of the two key arrays holds the result.

#### Scenario: A capacity larger than the bound buffers is rejected

- **WHEN** `Record` is called with an `elem_capacity` whose implied key-array size exceeds the bound key buffer
- **THEN** a `std::runtime_error` is thrown

#### Scenario: Zero capacity records nothing

- **WHEN** `Record` is called with `elem_capacity = 0`
- **THEN** no dispatch is recorded
- **AND** no exception is thrown
- **AND** the returned key buffer is the caller's input key buffer
