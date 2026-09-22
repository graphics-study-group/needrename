# gpu-compact-unique

## Purpose

Define the contract for a reusable GPU compact-unique post-processing pass (`CompactUnique` class and associated compute shaders). Given a sorted `uint` key array, it flags unique entries (those differing from their predecessor), performs a prefix sum to compute compacted positions, and scatters unique entries to a contiguous output. Follows the same self-contained module pattern as `ParallelScan` and `RadixSort`.

## Requirements

### Requirement: CompactUnique class construction

The `CompactUnique` class SHALL be constructible with a `RenderSystem&` and a `uint32_t max_elem_count`. The constructor SHALL NOT allocate any GPU resources. Shader loading SHALL be deferred until the first `AddPasses` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types.

#### Scenario: Construction defers GPU allocation

- **WHEN** `CompactUnique` is constructed with `RenderSystem& rs` and `max_elem_count = 10000`
- **THEN** no GPU resources are allocated
- **AND** no shaders are loaded
- **AND** `IsInitialized()` returns false

### Requirement: Static sizing helpers

`CompactUnique` SHALL expose static methods for scratch buffer sizing:

- `GetRequiredFlagBytes(uint32_t max_elem_count)` SHALL return `static_cast<size_t>(max_elem_count) * sizeof(uint32_t)` — the unique-flags buffer size.
- `GetRequiredScratchBytes()` SHALL return `sizeof(uint32_t)` (4 bytes) — the unique-count atomic counter.

#### Scenario: Flag buffer scales with element count

- **WHEN** `CompactUnique::GetRequiredFlagBytes(5000)` is called
- **THEN** the return value is `5000 * 4 = 20000` bytes

### Requirement: Single-call AddPasses API

`CompactUnique` SHALL expose a single `Record` method:

```cpp
void Record(
    vk::CommandBuffer cb,
    Rhi::ComputeBuffer &keys_buf,       // input: sorted keys; output: compacted unique keys
    Rhi::ComputeBuffer &flags_buf,      // original unique flags (max_elem_count uints)
    Rhi::ComputeBuffer &offsets_buf,    // flag offsets (prefix sum output, same size)
    Rhi::ComputeBuffer &count_buf,      // output unique count (1 uint)
    Rhi::ComputeBuffer &scan_scratch_buf,
    ParallelScan &scan,                 // external ParallelScan for the prefix sum
    Rhi::ComputeBuffer &elem_count_buf, // GPU-side element count, written upstream
    uint32_t elem_capacity              // buffer capacity (for dispatch sizing)
);
```

The method SHALL operate on a sorted array of `uint` **keys** and SHALL NOT know what those keys encode: it SHALL NOT decode, reconstruct or re-encode any record, and it SHALL NOT depend on any detector, solver or collision-specific type. Callers whose records are not scalars SHALL pack a record identity into the key themselves and restore it after compaction.

The method SHALL:

1. Flag unique entries → `flags_buf`: `flags[i] = (i == 0 || keys[i] != keys[i-1]) ? 1 : 0`
2. Copy `flags_buf` → `offsets_buf`
3. Perform an exclusive prefix sum on `offsets_buf` using the provided `ParallelScan` instance, in place. The scan SHALL use `elem_capacity` as the element count — zeros beyond the element count do not affect the result.
4. Clear `count_buf` to zero
5. Scatter unique entries: for each `i` where `flags_buf[i] == 1`, write `keys_buf[i]` to `keys_buf[offsets_buf[i]]`
6. Write the total unique count to `count_buf`

**Dispatch sizing**: every dispatch SHALL be sized for `elem_capacity`. The actual number of valid elements SHALL be read at GPU execution time from `elem_count_buf`, bound to the shader's `ElemCount` binding; invocations at or beyond it SHALL return immediately.

If `elem_capacity == 0`, the method SHALL record nothing.

`CompactUnique` SHALL NOT publish the compacted count anywhere except `count_buf`: propagating it into a caller's own count buffer is the caller's business, not the algorithm's.

#### Scenario: Duplicate pairs are compacted

- **WHEN** the sorted input keys are `[10, 10, 11, 12, 12, 12]` (6 entries, 3 unique)
- **THEN** after `Record`, `keys_buf` contains `[10, 11, 12]` in its first 3 slots
- **AND** `count_buf[0] = 3`

#### Scenario: Already-unique input is unchanged

- **WHEN** the sorted input keys are `[10, 11, 12]`
- **THEN** after `Record`, `keys_buf` contains `[10, 11, 12]`
- **AND** `count_buf[0] = 3`

#### Scenario: Empty capacity produces no passes

- **WHEN** `Record` is called with `elem_capacity = 0`
- **THEN** no dispatch is recorded

#### Scenario: Single element produces one unique

- **WHEN** the input has one key `[7]`
- **THEN** after `Record`, `keys_buf[0] == 7` and `count_buf[0] = 1`

#### Scenario: Threads beyond actual count are skipped

- **WHEN** `elem_capacity = 10000` but the GPU-side count is 50
- **THEN** thread 0 flags `flags[0] = 1` and threads 1-49 compare against their predecessors
- **AND** invocations at or beyond 50 return immediately without reading the key array

#### Scenario: The algorithm does not publish a caller's count

- **WHEN** `Record` completes
- **THEN** only `count_buf` holds the unique count
- **AND** no other buffer was written by the compaction

### Requirement: Element count via GPU buffer binding

The `flag_unique.comp`, `copy_uint.comp` (used internally), and `compact_scatter.comp` shaders SHALL each bind an `ElemCount` readonly buffer that provides the actual number of valid pairs at GPU execution time:

```glsl
layout(set = 0, binding = N) readonly buffer ElemCount {
    uint count;
} elem_count;
```

Each thread SHALL check `idx >= elem_count.count` and return immediately if true. The `ElemCount` buffer SHALL be the caller-provided `pair_count_buf` — a GPU buffer written by upstream pair-generation passes. At RenderGraph build time this buffer's value is NOT valid; the shader reads it at GPU execution time.

Dispatch workgroup count SHALL be calculated from `elem_capacity` (buffer capacity, e.g. `max_pairs`), not from the GPU-side pair count.

#### Scenario: ElemCount bound to GPU pair_count buffer

- **WHEN** `CompactUnique::AddPasses` builds the render graph
- **THEN** the `ElemCount` shader binding is connected to `pair_count_buf` (not a CPU-side constant buffer)
- **AND** `pair_count_buf` is the same buffer written atomically by the broad-phase pair generation passes

### Requirement: Flag unique shader

A `flag_unique.comp` compute shader SHALL mark each element's uniqueness by comparing its `uint` key with the previous element's. For the first element (`i == 0`) the flag SHALL be 1. Dispatch SHALL be `(ceil(elem_capacity / 64), 1, 1)` with local size 64.

#### Scenario: First element always flagged

- **WHEN** `flag_unique.comp` processes the sorted key array
- **THEN** `flags[0] = 1` regardless of value

#### Scenario: Duplicate detection

- **WHEN** `keys[i] == keys[i-1]`
- **THEN** `flags[i] = 0`

#### Scenario: New unique pair detection

- **WHEN** `keys[i] != keys[i-1]`
- **THEN** `flags[i] = 1`

### Requirement: Compact scatter shader

A `compact_scatter.comp` compute shader SHALL write unique keys to compact positions. Each thread SHALL read `flags[i]` (the original flag, before the prefix sum) and `offsets[i]` (the prefix-sum value). If `flags[i] == 1`, the thread SHALL write `keys[i]` to `keys[offsets[i]]`. The last thread SHALL write the total unique count to `count_buf[0]`.

#### Scenario: Scatter preserves order

- **WHEN** the input keys are `[10, 11, 12]` with flags `[1, 1, 1]` and offsets `[0, 1, 2]`
- **THEN** the scatter writes key `10` to slot 0, `11` to slot 1 and `12` to slot 2

#### Scenario: Scatter compacts out duplicates

- **WHEN** the input keys are `[10, 10, 11]` with flags `[1, 0, 1]` and offsets `[0, 1, 1]`
- **THEN** thread 0 writes key `10` to slot 0
- **AND** thread 1 writes nothing
- **AND** thread 2 writes key `11` to slot 1
- **AND** the total unique count is 2

### Requirement: Integration with ParallelScan

`CompactUnique::AddPasses` SHALL accept an external `ParallelScan&` reference for the prefix sum step. The caller is responsible for sizing and providing the scan scratch buffer. The method SHALL call `ParallelScan::AddPasses` to add the prefix-sum passes to the render graph.

#### Scenario: CompactUnique uses external ParallelScan

- **WHEN** `CompactUnique::AddPasses` is called with a `ParallelScan` instance
- **THEN** `ParallelScan::AddPasses` is called once with `elem_count`
- **AND** the scan processes the `flags` buffer in-place (exclusive prefix sum)
