# gpu-compact-unique

## MODIFIED Requirements

### Requirement: CompactUnique class construction

The `CompactUnique` class SHALL be constructible with `(Rhi::DeviceContext &device_context)` alone. Element geometry SHALL NOT be a construction-time parameter, and the instance SHALL hold no geometry state. The constructor SHALL NOT allocate any GPU resources. Shader loading, kernel acquisition and working-storage allocation SHALL be deferred until the first `Record` call.

The class SHALL reside in `engine/Physics/gpu_algorithm/` and SHALL NOT depend on any detector, solver, or collision-specific types.

The class SHALL own its working storage — the unique-flag array, the flag-offset array and a private `ParallelScan` with its own block sums — sized for the largest element capacity the instance has been recorded against and reused across calls: it SHALL grow when a call's capacity exceeds what it holds, SHALL NOT shrink when a later call is smaller, and SHALL NOT be reallocated by a call that fits. A caller SHALL provide only its own key array, its count buffer and its element-count buffer, and SHALL NOT size or allocate any working storage.

Because the working storage is shared by every call, two `Record` calls on one instance in the same command buffer SHALL have an execution-order dependency through it, and the class SHALL state the caller's barrier obligation in its interface documentation.

#### Scenario: Construction defers GPU allocation

- **WHEN** `CompactUnique` is constructed with a device context
- **THEN** no GPU resources are allocated
- **AND** no shaders are loaded
- **AND** `IsInitialized()` returns false

### Requirement: CompactUnique Record API

`CompactUnique` SHALL expose a single `Record` method:

```cpp
void Record(
    vk::CommandBuffer cb,
    Rhi::ComputeBuffer &keys_buf,       // input: sorted keys; output: compacted unique keys
    Rhi::ComputeBuffer &count_buf,      // output unique count (1 uint)
    Rhi::ComputeBuffer &elem_count_buf, // GPU-side element count, written upstream
    uint32_t elem_capacity              // buffer capacity (for dispatch sizing)
);
```

The method SHALL operate on a sorted array of `uint` **keys** and SHALL NOT know what those keys encode: it SHALL NOT decode, reconstruct or re-encode any record, and it SHALL NOT depend on any detector, solver or collision-specific type. Callers whose records are not scalars SHALL pack a record identity into the key themselves and restore it after compaction.

The method SHALL:

1. Flag unique entries into the instance's own flag buffer: `flags[i] = (i == 0 || keys[i] != keys[i-1]) ? 1 : 0`
2. Copy those flags into the instance's own offset buffer
3. Perform an exclusive prefix sum on that buffer using the instance's own `ParallelScan`, in place. The scan SHALL use `elem_capacity` as the element count — zeros beyond the element count do not affect the result.
4. Clear `count_buf` to zero
5. Scatter unique entries: for each `i` where `flags[i] == 1`, write `keys_buf[i]` to `keys_buf[offsets[i]]`
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
- **AND** no caller-provided buffer other than `keys_buf` and `count_buf` was written by the compaction

### Requirement: CompactUnique owns its ParallelScan

`CompactUnique` SHALL own the `ParallelScan` instance and the block-sums storage its prefix-sum step uses; both SHALL be created lazily on the first `Record` call and reused for the instance's lifetime. `Record` SHALL NOT accept a scan instance or a scan scratch buffer, and the caller SHALL NOT be responsible for sizing either.

#### Scenario: CompactUnique records its own scan

- **WHEN** `Record` is called
- **THEN** the instance's own `ParallelScan` performs the exclusive prefix sum on the flag offsets, in place
- **AND** no scan instance or scratch buffer is passed to the call
- **AND** the scan's block sums are the scan instance's own storage

## REMOVED Requirements

### Requirement: Static sizing helpers

**Reason**: The helpers existed so a caller could allocate the flag and offset arrays the compaction works in. Those arrays are working storage no caller can observe, so the class now owns and sizes them, and `GetRequiredScratchBytes()`'s "unique-count atomic counter" reports a buffer the implementation does not have: the unique count is published to the caller's `count_buf` and nowhere else. Keeping the helpers would leave two owners for one decision.

**Migration**: A caller passes only its key array, its count buffer, its element-count buffer and the capacity. `GetRequiredFlagBytes` and `GetRequiredScratchBytes` are deleted, together with the capacity-contract test that exercised the flag sizing.

## RENAMED Requirements

FROM: `Single-call AddPasses API`
TO: `CompactUnique Record API`

FROM: `Integration with ParallelScan`
TO: `CompactUnique owns its ParallelScan`
