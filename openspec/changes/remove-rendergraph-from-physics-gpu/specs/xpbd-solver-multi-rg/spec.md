## MODIFIED Requirements

### Requirement: GPUStep owns compute dispatch recording

`XpbdGpuSolver::GPUStep(vk::CommandBuffer cb)` SHALL record physics compute dispatches directly to `cb` without using `RenderGraph`. The method SHALL:

1. Read GPU buffers from `m_bound_scene`; early-return if no alive bodies
2. Insert a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) at the start to ensure preceding GPU work is visible
3. Notify `SceneDataManager::SetModelMatricesBuffer(gpu.model_matrices)`
4. Dispatch passes in sequence:
   ```
   for each substep:
       [entry barrier] PreCollision passes
       broad_detector->Record(cb)
       narrow_detector->Record(cb)
       [entry barrier] PostCollisionPreIter passes
       for each position iteration:
           PositionIter passes (each with entry barrier)
       [entry barrier] PostPosition passes
       for each velocity iteration:
           VelocityIter passes (each with entry barrier)
   [entry barrier] ModelMatrix pass
   ```

The PreCollision pass order SHALL be: substep-start position/orientation snapshots → integrate forces → pre-contact velocity snapshots → update shape world poses. This ensures pre-contact velocity snapshots capture post-integration velocity (including gravity) for correct restitution reference in the velocity solver.

All compute shader pipelines and resource bindings SHALL be pre-allocated in `PreGPUStep`. `GPUStep` SHALL NOT allocate any GPU resources.

#### Scenario: Compute dispatches recorded directly to command buffer

- **WHEN** `GPUStep(cb)` is called with a valid scene and simulation enabled
- **THEN** all solver compute dispatches, detector dispatches, and model matrix dispatch are recorded directly to `cb` via `cb.BindComputeStage`, `cb.BindComputeResource`, and `cb.DispatchCompute`
- **AND** no `RenderGraph::RecordAllPasses` or `RenderGraphBuilder::BuildRenderGraph` is called

#### Scenario: Entry barrier inserted at start of each phase

- **WHEN** `GPUStep(cb)` begins a new phase (PreCollision, PostCollisionPreIter, PositionIter, PostPosition, VelocityIter, ModelMatrix)
- **THEN** a `vk::MemoryBarrier2` is recorded before the first dispatch of that phase
- **AND** the barrier ensures all ShaderStorageWrite from the previous phase is visible

#### Scenario: Loop counts do not trigger any rebuild

- **WHEN** `substep_count`, `pos_iters`, or `vel_iters` change in `XpbdConfig`
- **THEN** the new loop counts are used in the next `GPUStep` call
- **AND** no internal state is rebuilt (no RG, no binding reallocation)

#### Scenario: BroadPhase Record called each substep

- **WHEN** `GPUStep` iterates over substeps
- **THEN** `broad_detector->Record(cb)` is called once per substep
- **AND** the detector internally records its compute dispatches with its own entry barrier

#### Scenario: NarrowPhase Record called each substep

- **WHEN** `GPUStep` iterates over substeps after broad-phase completes
- **THEN** `narrow_detector->Record(cb)` is called once per substep
- **AND** the detector internally records its compute dispatches with its own entry barrier

#### Scenario: Model matrix recorded unconditionally

- **WHEN** `PhysicsScene::IsSimulationEnabled()` returns `false`
- **THEN** ModelMatrix pass is still recorded
- **AND** other solver passes still dispatch (with time_step = 0, producing no position change)

## REMOVED Requirements

### Requirement: Cross-RG synchronization via prev_access

**Reason**: RenderGraph-based `prev_access` tracking is removed. Synchronization is handled by explicit `vk::MemoryBarrier2` at each module entry point.
**Migration**: Barrier is now inserted at the start of each Record() method. No `ImportExternalResource` or `prev_access` parameter is needed.

### Requirement: RGs rebuilt when body count changes

**Reason**: No RenderGraph exists to rebuild. Buffer resizing is handled by `EnsureIntermediateBuffers` in `PreGPUStep`. Workgroup counts are computed from current buffer sizes in `GPUStep`.
**Migration**: Remove `GpuStateSnapshot` and all RG rebuild detection logic.

### Requirement: RGs rebuilt when joint counts change

**Reason**: Same as above — no RG to rebuild.
**Migration**: Remove `GpuStateSnapshot` tracking of joint counts for RG rebuild purposes.
