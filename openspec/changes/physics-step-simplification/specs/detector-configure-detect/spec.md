# detector-configure-detect

## MODIFIED Requirements

### Requirement: Detector Record method dispatches compute directly

Each collision detector SHALL expose a `Record(vk::CommandBuffer cb)` method that records compute dispatches directly to `cb` through the compute kernel dispatch surface. The method SHALL insert a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) at the start.

`Record` SHALL prepare the detector for the geometry it observes — sizing its result buffers, acquiring the compute kernels it dispatches, and preparing its per-dispatch constants — before it records the first dispatch of the call, so that no caller-visible preparation call is required before `Record`. Preparation SHALL be a no-op when nothing it depends on has changed.

`Record` SHALL return `void`. Output buffer pointers SHALL be obtained via `GetResultBuffers()` (or equivalent const accessor).

`ConvexCollisionDetector::Record` SHALL NOT use `RenderGraph` or `RenderGraphBuilder`.

```cpp
void ConvexCollisionDetector::Record(vk::CommandBuffer cb);
void SpatialHashBroadDetector::Record(vk::CommandBuffer cb);
```

#### Scenario: Record dispatches compute passes directly

- **WHEN** `Record(cb)` is called
- **THEN** the detector records its compute dispatches directly to `cb` through the kernel dispatch surface
- **AND** no `RenderGraph::RecordAllPasses` is called

#### Scenario: Record creates no pipeline

- **WHEN** `Record(cb)` records its dispatches
- **THEN** every kernel the call dispatches was acquired before the first of those dispatches was recorded
- **AND** no pipeline or shader module is created between the call's first and last dispatch

#### Scenario: Record returns void

- **WHEN** `Record(cb)` completes
- **THEN** the method returns `void`
- **AND** output buffer pointers are available via `GetResultBuffers()`

#### Scenario: Entry barrier at Record start

- **WHEN** `Record(cb)` is called
- **THEN** a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) is recorded before the first dispatch
- **AND** no other barrier is inserted at the end of Record

#### Scenario: BroadPhase Record selects path with if-else

- **WHEN** `Record(cb)` is called and `shape_count <= fallback_all_pairs_threshold`
- **THEN** the fallback path is taken: AABB → fallback all-pairs directly
- **WHEN** `Record(cb)` is called and `shape_count > fallback_all_pairs_threshold`
- **THEN** the spatial hash path is taken: AABB → count cells → scan → fill cells → histogram → scan → scatter sort → generate pairs → generate global pairs → RadixSort → CompactUnique

#### Scenario: Record prepares itself on first call

- **WHEN** `Record(cb)` is called for the first time after the detector is bound to a scene
- **THEN** the detector sizes its result buffers and acquires its kernels during that call
- **AND** the dispatches are recorded in the same call

#### Scenario: Record absorbs a geometry change

- **WHEN** `Record(cb)` is called with a shape count or a pair capacity larger than the previous call's
- **THEN** the detector sizes its affected buffers during that call
- **AND** the replaced buffers' storage is released through the normal buffer-lifetime path
- **AND** the caller performs no preparation call between the two `Record` calls

## REMOVED Requirements

### Requirement: Detector Configure method handles CPU preparation

**Reason**: `Configure` existed as a separate caller-visible phase because buffer allocation had to happen while no command buffer was being recorded. `gpu-buffer-retirement` removes that constraint, and every input `Configure` consumed is a value the CPU holds when it records, so the phase is redundant with recording. Keeping it also forced callers to remember an ordering (`SpatialHashBroadDetector::Record` asserted that `Configure` had run) that the detector can enforce itself. The kernel-acquisition duty this requirement had accumulated is preserved in *Detector Record method dispatches compute directly*, which requires the kernels to be acquired before the call's first dispatch.

**Migration**: Delete the `Configure(...)` calls from `XpbdGpuSolver` and any other caller. The sizing parameters and input buffer references that were passed to `Configure` become the detector's own record-time inputs: a detector derives its geometry from the scene it is bound to and from its configuration, and prepares itself on the first `Record` and whenever that geometry changes. `GetResultBuffers()` continues to report output buffers and remains valid after the `Record` that produced them. The value-publishing duty moves from a CPU write to a host-visible buffer to a recorded push-constant block, per `physics-push-constants`.
