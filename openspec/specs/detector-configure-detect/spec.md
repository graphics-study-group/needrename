# detector-configure-detect

## Purpose

Define the refactored two-phase API for GPU collision detectors (`ConvexCollisionDetector` and `SpatialHashBroadDetector`), replacing the monolithic `AddDetectPasses(RenderGraphBuilder &, ...)` pattern with `Configure()` (CPU-side preparation) and `Detect(vk::CommandBuffer cb)` (GPU-side lazy RG build + record).

## Requirements

### Requirement: Detector Configure method handles CPU preparation

Each collision detector SHALL expose a `Configure(...)` method that performs all CPU-side work: validating input, resizing internal buffers, uploading uniform/configuration data to GPU-visible memory, and caching references for later `Detect` calls. `Configure` SHALL be safe to call every frame — it SHALL be a no-op when nothing changed.

`Configure` SHALL accept sizing parameters and input buffer references so the detector knows how large to make its result buffers and where to read input data. Detectors SHALL cache the `PhysicsScene*` reference and all input buffer pointers for use in `Detect`.

```cpp
void ConvexCollisionDetector::Configure(
    PhysicsScene &scene,
    uint32_t max_collision_pairs,
    float contact_margin,
    const ComputeBuffer &pair_buffer,
    const ComputeBuffer &pair_count_buffer
);

void SpatialHashBroadDetector::Configure(
    PhysicsScene &scene,
    uint32_t shape_count,
    GridConfig grid_config,
    uint32_t fallback_all_pairs_threshold
);
```

`ConvexCollisionDetector::Configure` SHALL cache `&pair_buffer` and `&pair_count_buffer` — these are the broad-phase detector's output buffers, and their addresses are stable after the broad-phase detector is first configured.

#### Scenario: Configure on first call creates buffers

- **WHEN** `Configure(scene, max_pairs, margin, pair_buf, count_buf)` is called for the first time
- **THEN** the detector allocates its result GPU buffers sized to `max_collision_pairs * 5`
- **AND** creates the detector config uniform buffer
- **AND** caches `&scene`, `&pair_buf`, and `&count_buf` for later `Detect` calls

#### Scenario: Configure resizes buffers when parameters change

- **WHEN** `Configure` is called with a larger `max_collision_pairs` than the previous call
- **THEN** result buffers are recreated at the new size
- **AND** the old buffers are destroyed

#### Scenario: Configure is a no-op when nothing changed

- **WHEN** `Configure` is called with the same parameters as the previous call
- **THEN** no buffer allocations or uploads occur
- **AND** the cached references remain valid

#### Scenario: Configure uploads CPU data to GPU

- **WHEN** `Configure` is called
- **THEN** `shape_slot_count` (for broad-phase) or `contact_margin` (for narrow-phase) is written to the detector's host-visible GPU buffer
- **AND** `GridConfig` data is uploaded for broad-phase

### Requirement: Detector Detect method owns its RenderGraph

Each collision detector SHALL expose a `Detect(vk::CommandBuffer cb)` method that lazily builds and records its own RenderGraph. The RG SHALL be cached and rebuilt only when parameters affecting its structure or buffer sizes change.

`Detect` SHALL import scene buffers directly from the cached `PhysicsScene*` using `builder.ImportExternalResource()`, and import the cached broad-phase pair buffers (`&pair_buffer`, `&pair_count_buffer`), specifying correct `prev_access` reflecting the state left by preceding RGs. `Detect` SHALL NOT accept `RenderGraphBuilder &`, `PhysicsSceneBufferHandles`, or pre-imported `RGBufferHandle` parameters.

`Detect` SHALL return a plain output struct containing raw `ComputeBuffer*` references to the detector's result buffers.

```cpp
CollisionResultBuffers ConvexCollisionDetector::Detect(vk::CommandBuffer cb);

BroadDetectorOutputBuffers SpatialHashBroadDetector::Detect(vk::CommandBuffer cb);
```

#### Scenario: Detect lazily builds RG on first call

- **WHEN** `Detect(cb)` is called for the first time after `Configure`
- **THEN** the detector creates a `RenderGraphBuilder`, imports scene and internal buffers, adds compute passes, and builds the RG
- **AND** `render_graph->RecordAllPasses(cb)` is called

#### Scenario: Detect rebuilds RG when buffer sizes change

- **WHEN** `Detect(cb)` is called after `Configure` changed `max_collision_pairs`
- **THEN** the detector rebuilds its RG with updated buffer sizes and workgroup counts

#### Scenario: Detect returns output buffers for solver consumption

- **WHEN** `Detect(cb)` completes
- **THEN** the returned `CollisionResultBuffers` struct contains valid `ComputeBuffer*` pointers to collision_ids, normals, contact_point_a, contact_point_b, and collision_count
- **AND** the solver uses these pointers to import the buffers into its own RGs

#### Scenario: BroadPhase Detect builds fallback or spatial-hash RG

- **WHEN** `Detect(cb)` is called and `shape_count <= fallback_all_pairs_threshold`
- **THEN** the detector builds a fallback RG (AABBs → fallback all-pairs)
- **WHEN** `Detect(cb)` is called and `shape_count > fallback_all_pairs_threshold`
- **THEN** the detector builds a spatial-hash RG (AABBs → count → scan → fill → hist → scan → scatter → pairs)

### Requirement: Detector shader loading is lazy

Shaders SHALL be loaded on the first call to `Detect` if not already loaded (or during `Configure` if detector prefers). Once loaded, `ComputeStage` and `ComputeResourceBinding` instances SHALL be reused across all subsequent calls.

#### Scenario: SPIR-V loaded once

- **WHEN** `Detect` is called the first time
- **THEN** SPIR-V files are loaded from disk
- **AND** `ComputeStage` instances are created
- **AND** subsequent `Detect` calls reuse the same stages

### Requirement: Detector Configure/Detect contract

`Configure` MUST be called at least once before the first `Detect`. Calling `Detect` without prior `Configure` is undefined behavior (implementation SHALL assert in debug builds). `Configure` SHALL be callable multiple times; subsequent calls update cached parameters and may trigger buffer resizes.

#### Scenario: Configure before Detect

- **WHEN** `Configure(scene, ...)` is called, then `Detect(cb)` is called
- **THEN** `Detect` SHALL have access to the cached `PhysicsScene*` and sizing parameters
- **AND** internal buffers SHALL be correctly sized
