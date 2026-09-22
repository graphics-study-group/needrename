# spatial-hash-broad-phase

## MODIFIED Requirements

### Requirement: Post-generation pair deduplication

After within-cell pair generation (`generate_broad_pairs.comp`) and global pair generation (`generate_global_pairs.comp`) complete, the detector SHALL perform deduplication on the combined candidate pair set before returning results to the caller.

A candidate pair is `(a, b)` with `a < b` and both indices below `shape_count`. The dedup SHALL encode each pair as the **packed key** `a * shape_count + b`, which is injective over that domain and order-preserving, and SHALL carry that key through the sort and the compaction; the canonical `uvec2` pair SHALL be restored before the results leave the detector, so the narrow phase's input format and the `pair.x < pair.y` invariant are unchanged.

The dedup SHALL proceed in three stages:

1. **Sort**: `RadixSort::Record` stably sorts the packed keys ascending, with `max_key_value = shape_count * shape_count - 1` and no payload array. The sorted result SHALL overwrite the key array.
2. **Compact**: `CompactUnique::Record` removes adjacent duplicates and compacts the unique keys, writing the unique count to `gpu_unique_count`.
3. **Unpack and publish**: `unpack_pairs.comp` reads the compacted keys and writes the canonical `uvec2(a, b)` pairs into `collision_pairs[]`, reconstructing `a = key / shape_count` and `b = key % shape_count`; the same pass SHALL publish `pair_count` from `gpu_unique_count`, replacing the separate count-copy dispatch.

`shape_count` SHALL be the same value in all three stages: the value the detector was configured with, which is also the value the pair-generation shaders use as their shape bound. The detector SHALL assert `shape_count <= 65536` when configured, because `shape_count * shape_count` is the packed key's bound and `shape_count * (shape_count - 1)` already wraps in 32-bit arithmetic at 65537.

**Dispatch sizing**: every stage SHALL pass `max_output_pair_count` (buffer capacity) as its element capacity for dispatch sizing. The actual pair count (`gpu_pair_count`) SHALL be passed as a GPU buffer binding — each shader reads it at execution time to skip threads beyond the valid range; the host SHALL NOT read it at record time.

The detector SHALL allocate the following buffers:
- `gpu_pairs_temp`: ping-pong temp for the key array (`max_output_pair_count × sizeof(uint32_t)`)
- `gpu_radix_scratch`: the radix sort's scratch, sized by `RadixSort::GetRequiredScratchBytes(max_output_pair_count)`
- `gpu_unique_flags`: original 0/1 flags (`max_output_pair_count × sizeof(uint32_t)`)
- `gpu_unique_offsets`: prefix-sum offsets, same size as the flags
- `gpu_unique_count`: the compacted unique count (`sizeof(uint32_t)`, host-visible)

The detector SHALL use its existing `ParallelScan` instance for `CompactUnique`'s internal prefix sum. The dedup SHALL be skipped if the fallback all-pairs path is used.

#### Scenario: Duplicate pairs are removed

- **WHEN** within-cell generation emits the packed keys for `(1,4)`, `(2,3)` and `(1,4)` again
- **AND** no global pairs are generated
- **THEN** after dedup, `collision_pairs[]` contains `(1,4)` and `(2,3)` as `uvec2`s
- **AND** `pair_count = 2`

#### Scenario: The unpacked pairs keep the canonical order

- **WHEN** a compacted key is unpacked with `shape_count = 200`
- **THEN** the pair's `.x` is the smaller shape index and `.y` is the larger one
- **AND** the reconstructed pair equals the pair that was packed

#### Scenario: Dedup dispatches for max_pairs, reads actual count from GPU

- **WHEN** the detector records the dedup section
- **THEN** the sort is recorded with `elem_capacity = max_output_pair_count` and the pair-count buffer
- **AND** the host does not read the pair count while recording

#### Scenario: A shape count above the packing bound is rejected

- **WHEN** the detector is configured with `shape_count = 65537`
- **THEN** an assertion fires on the host
- **AND** no dedup is recorded with a wrapped key bound
