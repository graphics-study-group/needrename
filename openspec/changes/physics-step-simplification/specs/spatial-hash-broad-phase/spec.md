# spatial-hash-broad-phase

## MODIFIED Requirements

### Requirement: SpatialHashBroadDetector class

The `SpatialHashBroadDetector` class SHALL be an independent broad-phase collision detector that owns GPU compute pipelines and buffers for spatial-hash-based candidate pair generation. It SHALL load its SPIR-V lazily on first use, own its compute pipelines and resource bindings, and record its dispatches directly to a caller-supplied command buffer through a `Record(vk::CommandBuffer cb)` method. It SHALL NOT own or build a `RenderGraph`.

The constructor SHALL accept `(Rhi::DeviceContext&)`. The detector SHALL prepare itself for the geometry it observes: on the first `Record` after binding to a scene, and thereafter whenever the shape count or the pair capacity changes, it SHALL size its internal buffers, create its bindings, and prepare its per-dispatch constants. No GPU resources SHALL be allocated before the first `Record`.

The detector SHALL expose:
```cpp
void Record(vk::CommandBuffer cb);
BroadDetectorOutputBuffers GetResultBuffers() const;
```

Detector preparation SHALL cache the bound `PhysicsScene*` and its sizing parameters. Configuration values that reach the shader SHALL travel in recorded push-constant blocks rather than in CPU writes to buffer memory.

`Record` SHALL select its pass sequence from the configured shape count: if `shape_count <= fallback_all_pairs_threshold`, the fallback sequence runs (AABBs → clear pair count → fallback all-pairs); otherwise the full spatial-hash sequence runs (using `ParallelScan` for prefix sums). `Record` SHALL read scene buffers through the cached `PhysicsScene*` and SHALL make its output pair buffers available through `GetResultBuffers()` as `ComputeBuffer*` references.

#### Scenario: Lazy initialization on first Detect call

- **WHEN** `SpatialHashBroadDetector::Record(cb)` is called for the first time
- **THEN** the detector loads all broad-phase SPIR-V files from `<ENGINE_PHYSICS_SPIRV_DIR>/collision/SpatialHashBroadDetector/`
- **AND** creates its compute pipelines and resource bindings for each shader
- **AND** records its dispatches to `cb`
- **AND** subsequent calls reuse the same pipelines

#### Scenario: Detector exposes output buffers to narrow-phase

- **WHEN** `SpatialHashBroadDetector::Record(cb)` completes
- **THEN** `GetResultBuffers()` reports a `BroadDetectorOutputBuffers` struct with `ComputeBuffer` references (`.pair_buffer`, `.pair_count_buffer`) and `.max_pairs`
- **AND** all buffers are owned by the detector and valid until the next call that resizes them or until detector destruction

#### Scenario: Detector returns empty result for insufficient shapes

- **WHEN** `Record(cb)` is called with `shape_slot_count <= 1`
- **THEN** the detector writes `pair_count = 0` and returns without dispatching any compute passes

#### Scenario: Fallback RG built for small N

- **WHEN** `shape_count <= fallback_all_pairs_threshold` at record time
- **THEN** the detector records the fallback sequence (AABBs + all-pairs generation only)
- **AND** no cell assignment, counting sort, or within-cell generation passes are recorded

#### Scenario: Spatial-hash RG built for large N

- **WHEN** `shape_count > fallback_all_pairs_threshold` at record time
- **THEN** the detector records the full spatial-hash sequence with all passes
- **AND** `ParallelScan` is used for both prefix-sum computations

### Requirement: Post-generation pair deduplication

After within-cell pair generation (`generate_broad_pairs.comp`) and global pair generation (`generate_global_pairs.comp`) complete, the detector SHALL perform deduplication on the combined candidate pair set before returning results to the caller.

A candidate pair is `(a, b)` with `a < b` and both indices below `shape_count`. The dedup SHALL encode each pair as the **packed key** `a * shape_count + b`, which is injective over that domain and order-preserving, and SHALL carry that key through the sort and the compaction; the canonical `uvec2` pair SHALL be restored before the results leave the detector, so the narrow phase's input format and the `pair.x < pair.y` invariant are unchanged.

The dedup SHALL proceed in three stages:

1. **Sort**: `RadixSort::Record` stably sorts the packed keys ascending, with `max_key_value = shape_count * shape_count - 1` and no payload array. The sorted result SHALL overwrite the key array.
2. **Compact**: `CompactUnique::Record` removes adjacent duplicates and compacts the unique keys, writing the unique count to `gpu_unique_count`.
3. **Unpack and publish**: `unpack_pairs.comp` reads the compacted keys and writes the canonical `uvec2(a, b)` pairs into `collision_pairs[]`, reconstructing `a = key / shape_count` and `b = key % shape_count`; the same pass SHALL publish `pair_count` from `gpu_unique_count`, replacing the separate count-copy dispatch.

`shape_count` SHALL be the same value in all three stages: the value the detector is configured with, which is also the value the pair-generation shaders use as their shape bound. The detector SHALL assert `shape_count <= 65536` when configured, because `shape_count * shape_count` is the packed key's bound and `shape_count * (shape_count - 1)` already wraps in 32-bit arithmetic at 65537.

**Dispatch sizing**: every stage SHALL pass `max_output_pair_count` (buffer capacity) as its element capacity for dispatch sizing. The actual pair count (`gpu_pair_count`) SHALL be passed as a GPU buffer binding — each shader reads it at execution time to skip threads beyond the valid range; the host SHALL NOT read it at record time.

**Counts are device-local.** The detector's count buffers — `gpu_pair_count`, `gpu_unique_count`, `gpu_total_assignments` and `gpu_global_count` — are written and read on the GPU only. No CPU code reads or writes them, so they SHALL be allocated as device-local buffers rather than host-visible ones.

The detector SHALL allocate the following buffers:
- `gpu_pairs_temp`: ping-pong temp for the key array (`max_output_pair_count × sizeof(uint32_t)`)
- `gpu_radix_scratch`: the radix sort's scratch, sized by `RadixSort::GetRequiredScratchBytes(max_output_pair_count)`
- `gpu_unique_flags`: original 0/1 flags (`max_output_pair_count × sizeof(uint32_t)`)
- `gpu_unique_offsets`: prefix-sum offsets, same size as the flags
- `gpu_unique_count`: the compacted unique count (`sizeof(uint32_t)`, device-local)

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

#### Scenario: Count buffers are device-local

- **WHEN** the detector allocates `gpu_pair_count`, `gpu_unique_count`, `gpu_total_assignments` or `gpu_global_count`
- **THEN** each is allocated without CPU access
- **AND** no CPU code in the detector reads or writes them
