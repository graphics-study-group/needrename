## MODIFIED Requirements

### Requirement: PairCount buffer binding for GPU-side element count

The `radix_block_histogram.comp` and `radix_scatter.comp` shaders SHALL each bind a `ElemCount` readonly buffer providing the actual number of valid elements at GPU execution time:

```glsl
layout(set = 0, binding = N) readonly buffer ElemCount {
    uint count;
} elem_count;
```

`radix_copy_back.comp` SHALL bind the same buffer the same way, because the number of elements a sort may write is the number it sorted, not the capacity it dispatched over.

An invocation whose element index is at or beyond that count SHALL contribute no histogram count, no rank, no copy and no other output write. In the two shaders that contain `barrier()` calls, that SHALL be expressed as predication over the whole body and SHALL NOT be an early `return`: a non-uniform early return before a barrier is undefined behaviour, and in the histogram sub-step it would also leave that invocation's transposed cell unwritten. An inactive invocation SHALL contribute a sentinel digit that no real element can produce, so that the rank computation and the per-warp histograms agree on which invocations are active.

`radix_copy_back.comp` contains no barrier and owns no shared state, so it SHALL express the same bound as an early `return` before any read or write, and an index at or beyond the count SHALL leave the caller's arrays exactly as they were.

Dispatch workgroup count SHALL be calculated from `elem_capacity`, never from the GPU-side count. The count buffer SHALL be a GPU buffer written by an upstream pass in the same frame and SHALL be read at execution time.

#### Scenario: Threads beyond actual count are skipped

- **WHEN** `elem_capacity = 10000` and the GPU-side count is 50
- **THEN** invocations with an index at or beyond 50 write no histogram count, claim no rank and write no output
- **AND** the sorted prefix is identical to a sort of those 50 elements alone

#### Scenario: The copy-back writes only the sorted prefix

- **WHEN** a copy-back runs with `elem_capacity = 10000` and a GPU-side count of 50
- **THEN** invocations with an index at or beyond 50 copy nothing
- **AND** the caller's key and payload arrays at or beyond 50 still hold what they held before the sort

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
| `radix_copy_back.comp` | `engine/Physics/shader/algorithm/radix_copy_back.comp` |

`radix_copy_back.comp` SHALL copy a completed sort's result out of the instance's partner arrays and back into the caller's arrays when a call's pass count is odd, copying the payload array alongside the key array when the call supplied one. It SHALL copy only the elements below the call's element count and SHALL NOT write the caller's arrays at or beyond it, so a sort never disturbs data the caller keeps past the elements it asked to sort. `radix_histogram.comp` and `radix_prefix_sum_256.comp` SHALL NOT exist: the per-block histogram replaces the global one, and the generic `ParallelScan` replaces the 256-element scan.

All of them SHALL compile to SPIR-V via `glslangValidator` and be placed in the build output at the physics SPIR-V root under `algorithm/`.

#### Scenario: Shaders compile to SPIR-V

- **WHEN** CMake is configured for the physics target
- **THEN** `radix_block_histogram.comp`, `radix_scatter.comp` and `radix_copy_back.comp` compile to `.spv` files in the physics SPIR-V output directory under `algorithm/`

#### Scenario: Removed shaders are gone

- **WHEN** the physics shader pipeline is configured
- **THEN** no `radix_histogram.comp.spv` and no `radix_prefix_sum_256.comp.spv` is produced
- **AND** `radix_block_histogram.comp.spv`, `radix_scatter.comp.spv` and `radix_copy_back.comp.spv` are produced

#### Scenario: The copy-back declares the element count it obeys

- **WHEN** the bindings of `radix_copy_back.comp` are inspected
- **THEN** it declares a readonly count buffer alongside its two source and two destination arrays
- **AND** the mirroring `Record` binds the call's element-count buffer to it
- **AND** the copy-back writes nothing at or beyond that count

### Requirement: Stable ascending sort with a derived pass count

The sort SHALL be **stable**: for any two elements `i < j` with equal keys, the element at `i` SHALL appear before the element at `j` in the output. Stability SHALL hold for every pass count, because LSD radix sort is correct only if each pass preserves the relative order established by the previous one.

The number of passes SHALL be derived from the caller's key bound and SHALL NOT be a fixed constant:

```
num_passes = ceil(bit_width(max_key_value) / 8)
```

A key bound that fits in one byte SHALL therefore produce exactly one pass, and a bound of zero or one SHALL produce no passes at all and leave the input untouched.

Because the pass count can be odd, the last pass can leave the sorted result in the instance's own partner array. `Record` SHALL therefore copy the result back into the caller's arrays whenever the derived pass count is odd — the payload array alongside the key array when one was supplied — so that after every call the caller's own arrays hold the sorted records, whatever the pass count. The caller SHALL NOT need to know the pass count, and a later `Record` on the same instance SHALL NOT be able to overwrite an earlier call's result.

The copy-back SHALL be bounded by the call's element count, which is the number of elements the sort ordered. Whatever a caller keeps in its arrays at or beyond that count SHALL survive every `Record` unchanged, so a caller that writes a full-capacity tail and sorts a shorter prefix keeps that tail — and a sort SHALL NOT be able to introduce a value of its own into a slot the caller depends on.

#### Scenario: Equal keys keep their input order

- **WHEN** keys are `[3, 1, 3, 1, 3, 1]` with payloads equal to their input indices, over a capacity that spans several blocks
- **THEN** the output keys are `[1, 1, 1, 3, 3, 3]`
- **AND** the payloads of the three `1`s are `1, 3, 5` in that order
- **AND** the payloads of the three `3`s are `0, 2, 4` in that order

#### Scenario: One pass for a one-byte key domain

- **WHEN** `Record` is called with `max_key_value = 200`
- **THEN** exactly one pass is recorded (three dispatches: histogram, scan, scatter)
- **AND** one further copy-back dispatch is recorded, because a one-pass sort leaves its result in the partner array

#### Scenario: A small bound produces no passes

- **WHEN** `Record` is called with `max_key_value <= 1`
- **THEN** no dispatch is recorded
- **AND** the caller's arrays are left untouched and already hold the result

#### Scenario: The caller's tail survives an odd pass count

- **WHEN** a caller's arrays hold sentinel values at or beyond the element count, as a full-capacity entry pass leaves them
- **AND** a sort whose pass count is odd runs over the elements below that count
- **THEN** the sorted prefix holds the sorted records
- **AND** every slot at or beyond the element count still holds the sentinel value it held before the sort

#### Scenario: The result buffer is returned, not assumed

The scenario name is retained from the contract that returned the result buffers; `Record` no longer returns anything, because the answer is no longer in doubt. Its subject is unchanged: the caller binds the sorted result without knowing the pass count.

- **WHEN** `Record` is called with an odd pass count and again with an even one
- **THEN** the caller's key array holds the sorted records after each call
- **AND** the caller does not need to know the pass count to bind the sorted result
