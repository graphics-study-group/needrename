# detector-configure-detect

## MODIFIED Requirements

### Requirement: Detector Record method dispatches compute directly

Each collision detector SHALL expose a `Record(vk::CommandBuffer cb)` method that records compute dispatches directly to `cb` through the compute kernel dispatch surface. The method SHALL insert a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) at the start.

`Record` SHALL return `void`. Output buffer pointers SHALL be obtained via `GetResultBuffers()` (or equivalent const accessor).

`ConvexCollisionDetector::Record` SHALL NOT use `RenderGraph` or `RenderGraphBuilder`. All compute kernels SHALL be acquired in `Configure`, so that `Record` creates no pipeline and no shader module.

```cpp
void ConvexCollisionDetector::Record(vk::CommandBuffer cb);
void SpatialHashBroadDetector::Record(vk::CommandBuffer cb);
```

#### Scenario: Record dispatches compute passes directly

- **WHEN** `Record(cb)` is called after `Configure`
- **THEN** the detector records its compute dispatches directly to `cb` through the kernel dispatch surface
- **AND** no `RenderGraph::RecordAllPasses` is called

#### Scenario: Record creates no pipeline

- **WHEN** `Record(cb)` is called
- **THEN** every kernel it dispatches was already acquired during `Configure`
- **AND** no pipeline or shader module is created during recording

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

### Requirement: Detector Configure method handles CPU preparation

Each collision detector SHALL expose a `Configure(...)` method that performs all CPU-side work: validating input, resizing internal buffers, uploading uniform/configuration data to GPU-visible memory, acquiring the compute kernels it dispatches, and caching references for later `Record` calls. `Configure` SHALL be safe to call every frame — it SHALL be a no-op when nothing changed.

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
- **AND** acquires a compute kernel for each shader module it dispatches
- **AND** caches `&scene`, `&pair_buf`, and `&count_buf` for later `Record` calls

#### Scenario: Configure resizes buffers when parameters change

- **WHEN** `Configure` is called with a larger `max_collision_pairs` than the previous call
- **THEN** result buffers are recreated at the new size
- **AND** the old buffers are released through the normal buffer-lifetime path
- **AND** no per-detector binding object needs re-creating, because resources are supplied per dispatch

#### Scenario: Configure is a no-op when nothing changed

- **WHEN** `Configure` is called with the same parameters as the previous call
- **THEN** no buffer allocations, kernel acquisitions, or uploads occur
- **AND** the cached references remain valid

#### Scenario: Configure uploads CPU data to GPU

- **WHEN** `Configure` is called
- **THEN** `shape_slot_count` (for broad-phase) or `contact_margin` (for narrow-phase) is written to the detector's host-visible GPU buffer
- **AND** `GridConfig` data is uploaded for broad-phase

## REMOVED Requirements

### Requirement: Detector binding allocation in Configure

**Reason**: The requirement is entirely about `ComputeResourceBinding`, which this change deletes. It existed to keep descriptor-set allocation out of the recording path, because allocating a set per dispatch was considered costly. After the change, a dispatch supplies its resources as a dictionary and the descriptor set for that combination comes from the device's descriptor arena, keyed by the current submission epoch — so there is no per-detector binding object to pre-allocate, and a buffer resize no longer implies re-creating one. The intent that `Record` must not create pipelines is preserved by the *Detector Record method dispatches compute directly* requirement above.

**Migration**: Delete each detector's binding members and the code that creates them in `Configure`; instead acquire the kernels in `Configure` and pass the resource dictionary at each dispatch in `Record`. A resized buffer needs no binding bookkeeping at all: the dictionary is rebuilt from the current buffer handles on every dispatch. Verify with the *Record creates no pipeline* scenario.
