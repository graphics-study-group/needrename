# gpu-compact-unique

## Purpose

Define the contract for a reusable GPU compact-unique post-processing pass (`CompactUnique` class and associated compute shaders). Given a sorted `uvec2` array, it flags unique entries (those differing from their predecessor), performs a prefix sum to compute compacted positions, and scatters unique entries to a contiguous output. Follows the same self-contained module pattern as `ParallelScan` and `RadixSort`.

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

`CompactUnique` SHALL expose an `AddPasses` method:

```cpp
void AddPasses(
    RenderGraphBuilder &builder,
    RGBufferHandle pairs_handle,        // input pairs (sorted), output unique pairs
    ComputeBuffer &pairs_buf,
    RGBufferHandle flags_handle,        // original unique flags (max_elem_count uints)
    ComputeBuffer &flags_buf,
    RGBufferHandle offsets_handle,      // flag offsets (prefix sum output, same size)
    ComputeBuffer &offsets_buf,
    RGBufferHandle count_handle,        // output unique count (1 uint)
    ComputeBuffer &count_buf,
    RGBufferHandle scan_scratch_handle, // ParallelScan scratch
    ComputeBuffer &scan_scratch_buf,
    ParallelScan &scan,                 // external ParallelScan for prefix sum
    RGBufferHandle pair_count_handle,   // RG handle for the actual pair count buffer
    ComputeBuffer &pair_count_buf,      // GPU-side uint, written by upstream passes
    uint32_t elem_capacity              // buffer capacity (for dispatch sizing)
);
```

The method SHALL:
1. Flag unique entries → `flags_buf`: `flags[i] = (i == 0 || pairs[i] != pairs[i-1]) ? 1 : 0`
2. Copy `flags_buf` → `offsets_buf` (using `copy_uint.comp`)
3. Perform exclusive prefix sum on `offsets_buf` using the provided `ParallelScan` instance (in-place). The scan SHALL use `elem_capacity` as the element count — zeros beyond `pair_count` do not affect the prefix sum result.
4. Clear `count_buf` to zero
5. Scatter unique entries: for each `i` where `flags_buf[i] == 1`, write `pairs_buf[i]` to `pairs_buf[offsets_buf[i]]`
6. Write total unique count to `count_buf`

**Dispatch sizing**: All workgroup dispatches SHALL be sized for `elem_capacity` (buffer capacity). The actual number of valid pairs is read at GPU execution time from `pair_count_buf`, bound to the shader's `ElemCount` binding. Threads with `idx >= pair_count.count` SHALL return immediately.

**Why two separate buffers**: The `flags_buf` holds the original 0/1 flags (needed for scatter), while `offsets_buf` holds the post-scan exclusive prefix sum (needed for write positions). Since the prefix sum runs in-place on `offsets_buf`, we must keep the original flags in a separate buffer. A copy pass moves `flags_buf` → `offsets_buf` before the scan.

If `elem_capacity == 0`, the method SHALL return immediately.

#### Scenario: Duplicate pairs are compacted

- **WHEN** input sorted pairs are `[(1,4), (1,4), (2,3), (3,7), (3,7), (3,7)]` (6 entries, 3 unique)
- **THEN** after `AddPasses`, `pairs_buf` contains `[(1,4), (2,3), (3,7)]` in the first 3 slots
- **AND** `count_buf[0] = 3`

#### Scenario: Already-unique input is unchanged

- **WHEN** input sorted pairs are `[(1,4), (2,3), (3,7)]` (3 entries, all unique)
- **THEN** after `AddPasses`, `pairs_buf` contains `[(1,4), (2,3), (3,7)]`
- **AND** `count_buf[0] = 3`

#### Scenario: Empty capacity produces no passes

- **WHEN** `AddPasses` is called with `elem_capacity = 0`
- **THEN** no compute passes are added to the builder

#### Scenario: Single element produces one unique

- **WHEN** input has 1 pair `[(5,9)]`
- **THEN** after `AddPasses`, `pairs_buf[0] == (5,9)`
- **AND** `count_buf[0] = 1`

#### Scenario: Threads beyond actual count are skipped

- **WHEN** `elem_capacity = 10000` but GPU-side `pair_count.count = 50`
- **THEN** thread 0 marks `flags[0] = 1` and threads 1-49 compare against predecessors
- **AND** threads with `idx >= 50` return immediately without reading from `SortedPairs`

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

A `flag_unique.comp` compute shader SHALL mark each element's uniqueness by comparing it with the previous element. For the first element (`i == 0`), the flag SHALL be 1. Dispatch SHALL be `(ceil(elem_capacity / 64), 1, 1)` with local size 64.

#### Scenario: First element always flagged

- **WHEN** `flag_unique.comp` processes the sorted array
- **THEN** `flags[0] = 1` regardless of value

#### Scenario: Duplicate detection

- **WHEN** `pairs[i] == pairs[i-1]` (both elements of the uvec2 identical)
- **THEN** `flags[i] = 0`

#### Scenario: New unique pair detection

- **WHEN** `pairs[i] != pairs[i-1]` in either x or y component
- **THEN** `flags[i] = 1`

### Requirement: Compact scatter shader

A `compact_scatter.comp` compute shader SHALL write unique entries to compact positions. Each thread SHALL read `flags[i]` (the original flag before prefix sum) and `offsets[i]` (the prefix-sum value). If `flags[i] == 1`, the thread SHALL write `pairs[i]` to `pairs[offsets[i]]`. The last thread SHALL write `offsets[elem_count-1] + flags[elem_count-1]` to `count_buf[0]` as the total unique count.

#### Scenario: Scatter preserves order

- **WHEN** input pairs are `[(1,4), (2,3), (3,7)]` and unique flags after prefix sum are `[0, 1, 2]`
- **THEN** the scatter writes `pairs[0]` to `output[0]`, `pairs[1]` to `output[1]`, `pairs[2]` to `output[2]`

#### Scenario: Scatter compacts out duplicates

- **WHEN** input pairs are `[(1,4), (1,4), (2,3)]` and original flags are `[1, 0, 1]` with prefix sums `[0, 1, 1]`
- **THEN** thread 0 writes `pairs[0]` to `output[0]` (flag=1)
- **AND** thread 1 does not write (flag=0)
- **AND** thread 2 writes `pairs[2]` to `output[1]` (flag=1, offset=1)

### Requirement: Integration with ParallelScan

`CompactUnique::AddPasses` SHALL accept an external `ParallelScan&` reference for the prefix sum step. The caller is responsible for sizing and providing the scan scratch buffer. The method SHALL call `ParallelScan::AddPasses` to add the prefix-sum passes to the render graph.

#### Scenario: CompactUnique uses external ParallelScan

- **WHEN** `CompactUnique::AddPasses` is called with a `ParallelScan` instance
- **THEN** `ParallelScan::AddPasses` is called once with `elem_count`
- **AND** the scan processes the `flags` buffer in-place (exclusive prefix sum)
