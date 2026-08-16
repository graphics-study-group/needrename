# xpbd-solver-multi-rg

## Purpose

Define the `XpbdGpuSolver` class as an `ISolver` implementation that records physics compute dispatches directly to the command buffer (no RenderGraph). Cover the three-phase lifecycle (PreGPUStep, GPUStep, PostGPUStep), explicit `vk::MemoryBarrier2` synchronization at phase boundaries, and pre-allocated pipelines/bindings.

## Requirements

### Requirement: XpbdGpuSolver inherits ISolver

The system SHALL provide an `XpbdGpuSolver` class inheriting `ISolver`, defined in `engine/Physics/Solver/XpbdGpuSolver.h`. The constructor SHALL accept `RenderSystem &` only. `PhysicsScene` access SHALL be obtained through `m_bound_scene` (set by `OnBindToScene`).

The class SHALL NOT expose any `AddStepPasses()` or `RenderGraphBuilder &` — callers interact only through `PreGPUStep()` and `GPUStep(cb)`.

```cpp
class XpbdGpuSolver : public ISolver {
public:
    explicit XpbdGpuSolver(RenderSystem &render_system);
    ~XpbdGpuSolver() override;

    void PreGPUStep() override;
    void GPUStep(vk::CommandBuffer cb) override;
    bool IsInitialized() const noexcept override;

    void SetConfig(const XpbdConfig &config) noexcept;
    const XpbdConfig &GetConfig() const noexcept;
};
```

#### Scenario: Solver registered via PhysicsSystem

- **WHEN** `PhysicsSystem::RegisterSolver(scene_id, std::make_unique<XpbdGpuSolver>(render_system))` is called
- **THEN** `XpbdGpuSolver::OnBindToScene(scene)` is invoked, setting `m_bound_scene`
- **AND** the solver appears in the per-frame `PreGPUStep` / `GPUStep` dispatch

#### Scenario: Solver constructor does not allocate GPU resources

- **WHEN** `XpbdGpuSolver` is constructed with a valid `RenderSystem`
- **THEN** no `ComputeStage`, `ComputeBuffer`, or RenderGraph objects are created
- **AND** `IsInitialized()` returns `false`

### Requirement: PreGPUStep handles CPU-side preparation

`XpbdGpuSolver::PreGPUStep()` SHALL perform all CPU-side work that must complete before command buffer recording:

1. Read GPU buffers from `m_bound_scene`; early-return if no alive bodies
2. Lazy shader loading: on first call, load all SPIR-V files and create `ComputeStage` instances
3. Ensure intermediate buffers are correctly sized (body_count, max_contacts, joint counts)
4. Upload uniform data (gravity_dt) to host-visible GPU buffer
5. Pre-allocate all compute shader pipelines and resource bindings for every phase (solver passes, detectors, model matrix pass)
6. Call `broad_detector->Configure(scene, shape_count, grid_config, fallback_threshold)` to ensure broad-phase buffers are sized and CPU data uploaded
7. Retrieve `BroadDetectorOutputBuffers` from broad detector to get `pair_buffer` and `pair_count_buffer` pointers
8. Call `narrow_detector->Configure(scene, max_collision_pairs, contact_margin, broad_bufs.pair_buffer, broad_bufs.pair_count_buffer)` to tell the narrow detector where to read candidate collision pairs from

#### Scenario: First PreGPUStep loads all shaders

- **WHEN** `PreGPUStep()` is called for the first time on a scene with at least one rigid body
- **THEN** all required SPIR-V files are loaded from `<ENGINE_PHYSICS_SPIRV_DIR>/solver/`
- **AND** `ComputeStage` instances are created for all solver shaders
- **AND** `IsInitialized()` returns `true` thereafter

#### Scenario: Buffer resize on body count change

- **WHEN** `PreGPUStep()` detects that `body_count` has changed since last call
- **THEN** intermediate buffers (snapshots, deltas, Lagrange multipliers) are recreated at the new size
- **AND** intermediate buffer sizes for joints and contacts are also recreated if those counts changed

#### Scenario: Detector Configure called each PreGPUStep

- **WHEN** `PreGPUStep()` is called
- **THEN** `broad_detector->Configure(scene, shape_count)` is called first, which ensures broad-phase internal buffers are correctly sized
- **AND** the solver retrieves broad-phase output buffer pointers (`pair_buffer`, `pair_count_buffer`) from the broad detector
- **AND** `narrow_detector->Configure(scene, max_pairs, margin, pair_buf, count_buf)` is called second, which caches those input buffer references

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
