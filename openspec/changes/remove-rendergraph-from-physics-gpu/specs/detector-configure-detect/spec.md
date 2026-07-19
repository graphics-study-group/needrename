## ADDED Requirements

### Requirement: Detector Record method dispatches compute directly

Each collision detector SHALL expose a `Record(vk::CommandBuffer cb)` method that records compute dispatches directly to `cb` using `cb.BindComputeStage`, `cb.BindComputeResource`, and `cb.DispatchCompute`. The method SHALL insert a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) at the start.

`Record` SHALL return `void`. Output buffer pointers SHALL be obtained via `GetResultBuffers()` (or equivalent const accessor).

`ConvexCollisionDetector::Record` SHALL NOT use `RenderGraph` or `RenderGraphBuilder`. All compute pipelines and resource bindings SHALL be pre-allocated in `Configure`.

```cpp
void ConvexCollisionDetector::Record(vk::CommandBuffer cb);
void SpatialHashBroadDetector::Record(vk::CommandBuffer cb);
```

#### Scenario: Record dispatches compute passes directly

- **WHEN** `Record(cb)` is called after `Configure`
- **THEN** the detector records its compute dispatches directly to `cb` via `cb.BindComputeStage`, `cb.BindComputeResource`, `cb.DispatchCompute`
- **AND** no `RenderGraph::RecordAllPasses` is called

#### Scenario: Record returns void

- **WHEN** `Record(cb)` completes
- **THEN** the method returns `void`
- **AND** output buffer pointers are available via `GetResultBuffers()`

#### Scenario: Entry barrier at Record start

- **WHEN** `Record(cb)` is called
- **THEN** a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) is recorded before the first dispatch
- **AND** no other barrier is inserted at the end of Record

#### Scenario: BroadPhase Record selects path with if-else

- **WHEN** `Record(cb)` is called and `shape_count <= fallback_all_pairs_threshold` (cached from Configure)
- **THEN** the fallback path is taken: AABB → fallback all-pairs directly
- **WHEN** `Record(cb)` is called and `shape_count > fallback_all_pairs_threshold`
- **THEN** the spatial hash path is taken: AABB → count cells → scan → fill cells → histogram → scan → scatter sort → generate pairs → generate global pairs → RadixSort → CompactUnique

### Requirement: Detector binding allocation in Configure

Each detector SHALL allocate all `ComputeResourceBinding` instances during `Configure`, after buffer allocation. `Record` SHALL NOT allocate bindings.

#### Scenario: Bindings pre-allocated in Configure

- **WHEN** `Configure` is called
- **THEN** all `ComputeResourceBinding` instances are created for all shader stages
- **AND** `Record` reuses these bindings without allocation

#### Scenario: Bindings rebuilt on buffer resize

- **WHEN** `Configure` triggers buffer resize
- **THEN** affected bindings are reallocated with updated `VkBuffer` handles

## MODIFIED Requirements

### Requirement: Detector Configure method handles CPU preparation

Each collision detector SHALL expose a `Configure(...)` method that performs all CPU-side work: validating input, resizing internal buffers, uploading uniform/configuration data to GPU-visible memory, creating shader pipelines and resource bindings, and caching references for later `Record` calls. `Configure` SHALL be safe to call every frame — it SHALL be a no-op when nothing changed.

`Configure` SHALL accept sizing parameters and input buffer references so the detector knows how large to make its result buffers and where to read input data. Detectors SHALL cache the `PhysicsScene*` reference and all input buffer pointers for use in `Record`.

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

#### Scenario: Configure on first call creates buffers and bindings

- **WHEN** `Configure(scene, max_pairs, margin, pair_buf, count_buf)` is called for the first time
- **THEN** the detector allocates its result GPU buffers sized to `max_collision_pairs * 5`
- **AND** creates the detector config uniform buffer
- **AND** creates all `ComputeResourceBinding` instances for its shader stages
- **AND** caches `&scene`, `&pair_buf`, and `&count_buf` for later `Record` calls

#### Scenario: Configure resizes buffers when parameters change

- **WHEN** `Configure` is called with a larger `max_collision_pairs` than the previous call
- **THEN** result buffers are recreated at the new size
- **AND** affected resource bindings are reallocated
- **AND** the old buffers are destroyed

#### Scenario: Configure is a no-op when nothing changed

- **WHEN** `Configure` is called with the same parameters as the previous call
- **THEN** no buffer allocations, binding allocations, or uploads occur
- **AND** the cached references remain valid

#### Scenario: Configure uploads CPU data to GPU

- **WHEN** `Configure` is called
- **THEN** `shape_slot_count` (for broad-phase) or `contact_margin` (for narrow-phase) is written to the detector's host-visible GPU buffer
- **AND** `GridConfig` data is uploaded for broad-phase

## REMOVED Requirements

### Requirement: Detector Detect method owns its RenderGraph

**Reason**: RenderGraph is removed. Direct compute dispatch via `Record(cb)` replaces `Detect`'s lazy RG build + record pattern.
**Migration**: Replace `Detect(cb)` calls with `Record(cb)`. Use `GetResultBuffers()` instead of `Detect` return values for output buffer access.

### Requirement: Detector shader loading is lazy

**Reason**: Shader loading, pipeline creation, and binding allocation all happen in `Configure`, not on first `Record`. This is a simpler, more predictable lifecycle.
**Migration**: Shaders are loaded during `Configure` if not already loaded, together with other GPU resource allocation.

### Requirement: Detector Configure/Detect contract

**Reason**: The Configure/Detect naming is obsolete. The new contract is Configure/Record.
**Migration**: `Configure` MUST be called at least once before the first `Record`. Calling `Record` without prior `Configure` asserts in debug builds.
