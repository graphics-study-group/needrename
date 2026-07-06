# spatial-hash-broad-phase

## MODIFIED Requirements

### Requirement: SpatialHashBroadDetector class

The `SpatialHashBroadDetector` class SHALL be an independent broad-phase collision detector that owns GPU compute pipelines and buffers for spatial-hash-based candidate pair generation. It SHALL follow the new detector pattern: lazy SPIR-V loading on first `Detect` call, `ComputeStage` ownership, `ComputeResourceBinding` management, and self-owned RenderGraph recording via a `Detect(vk::CommandBuffer cb)` method.

The constructor SHALL accept a `RenderSystem &` only. Sizing and configuration parameters SHALL be passed to `Configure()`. No GPU resources SHALL be allocated until `Configure` or `Detect` is first called.

The detector SHALL expose a two-phase API:
```cpp
void Configure(
    PhysicsScene &scene,
    uint32_t shape_count,
    GridConfig grid_config,
    uint32_t fallback_all_pairs_threshold
);
BroadDetectorOutputBuffers Detect(vk::CommandBuffer cb);
```

`Configure` SHALL cache `&scene` and all sizing parameters, ensure internal buffers are sized, and upload grid config and shape slot count to host-visible GPU buffers.

`Detect` SHALL lazily build the detector's own RenderGraph. The RG structure SHALL be determined at build time: if `shape_count <= fallback_all_pairs_threshold`, a fallback RG is built (AABBs → clear pair count → fallback all-pairs); otherwise the full spatial-hash RG is built (using ParallelScan utility for prefix sums). `Detect` SHALL import scene buffers directly from the cached `PhysicsScene*`, record all passes to `cb`, and return raw `ComputeBuffer*` references to the output pair buffers.

#### Scenario: Lazy initialization on first Detect call

- **WHEN** `SpatialHashBroadDetector::Detect(cb)` is called for the first time after `Configure`
- **THEN** the detector loads all broad-phase SPIR-V files from `<ENGINE_PHYSICS_SPIRV_DIR>/solver/SpatialHashBroadDetector/`
- **AND** creates `ComputeStage` and `ComputeResourceBinding` instances for each shader
- **AND** builds its RenderGraph and records it to `cb`
- **AND** subsequent calls reuse the same pipelines

#### Scenario: Detector exposes output buffers to narrow-phase

- **WHEN** `SpatialHashBroadDetector::Detect(cb)` completes
- **THEN** it returns a `BroadDetectorOutputBuffers` struct with raw `ComputeBuffer` references (`.pair_buffer`, `.pair_count_buffer`) and `.max_pairs`
- **AND** all buffers are owned by the detector and valid until the next call or detector destruction

#### Scenario: Detector returns empty result for insufficient shapes

- **WHEN** `Detect(cb)` is called with `shape_slot_count <= 1`
- **THEN** the detector writes `pair_count = 0` and returns without dispatching any compute passes

#### Scenario: Fallback RG built for small N

- **WHEN** `shape_count <= fallback_all_pairs_threshold` at RG build time
- **THEN** the detector builds a fallback RG (AABBs + all-pairs generation only)
- **AND** no cell assignment, counting sort, or within-cell generation passes are present in the RG

#### Scenario: Spatial-hash RG built for large N

- **WHEN** `shape_count > fallback_all_pairs_threshold` at RG build time
- **THEN** the detector builds the full spatial-hash RG with all passes
- **AND** ParallelScan is used as a utility function during RG build to add prefix sum passes

## REMOVED Requirements

### Requirement: Grid configuration and validation

**Reason**: Grid configuration validation is now handled in `Configure()` instead of the constructor. The `GridConfig` is passed to `Configure()`, which validates and uploads it.

**Migration**: Pass `GridConfig` to `Configure(scene, shape_count, grid_config, threshold)` instead of the constructor.
