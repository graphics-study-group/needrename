# xpbd-solver-multi-rg

## Purpose

Define the `XpbdGpuSolver` class as an `ISolver` implementation that owns multiple RenderGraphs, each corresponding to a distinct physics phase. Cover the three-phase lifecycle (PreGPUStep, GPUStep, PostGPUStep), lazy RG construction, cross-RG buffer synchronization via `prev_access`, and reconstruction triggers.

## ADDED Requirements

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
- **THEN** no `ComputeStage`, `ComputeBuffer`, or `RenderGraph` objects are created
- **AND** `IsInitialized()` returns `false`

### Requirement: PreGPUStep handles CPU-side preparation

`XpbdGpuSolver::PreGPUStep()` SHALL perform all CPU-side work that must complete before command buffer recording:

1. Read GPU buffers from `m_bound_scene`; early-return if no alive bodies
2. Lazy shader loading: on first call, load all SPIR-V files and create `ComputeStage` instances
3. Ensure intermediate buffers are correctly sized (body_count, max_contacts, joint counts)
4. Upload uniform data (gravity_dt) to host-visible GPU buffer
5. Call `broad_detector->Configure(scene, shape_count, grid_config, fallback_threshold)` to ensure broad-phase buffers are sized and CPU data uploaded
6. Retrieve `BroadDetectorOutputBuffers` from broad detector to get `pair_buffer` and `pair_count_buffer` pointers
7. Call `narrow_detector->Configure(scene, max_collision_pairs, contact_margin, broad_bufs.pair_buffer, broad_bufs.pair_count_buffer)` to tell the narrow detector where to read candidate collision pairs from

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

### Requirement: GPUStep owns multi-RG recording

`XpbdGpuSolver::GPUStep(vk::CommandBuffer cb)` SHALL record physics compute passes to `cb` using independently-managed RenderGraphs. The method SHALL:

1. Read GPU buffers from `m_bound_scene`; early-return if no alive bodies
2. Build any RGs that are null or have stale cached parameters (body_count, max_contacts, joint_counts, shape_count)
3. Notify `SceneDataManager::SetModelMatricesBuffer(gpu.model_matrices)`
4. Record RGs in sequence:
   ```
   for each substep:
       PreCollisionRG.RecordAllPasses(cb)
       broad_detector->Detect(cb)
       narrow_detector->Detect(cb)
       PostCollisionPreIterRG.RecordAllPasses(cb)
       for each position iteration:
           PositionIterRG.RecordAllPasses(cb)
       PostPositionRG.RecordAllPasses(cb)
       for each velocity iteration:
           VelocityIterRG.RecordAllPasses(cb)
   ModelMatrixRG.RecordAllPasses(cb)
   ```

#### Scenario: RGs built lazily on first GPUStep

- **WHEN** `GPUStep(cb)` is called for the first time with a valid scene
- **THEN** all 6 solver RGs are built via their respective `Build*RenderGraph()` methods
- **AND** the RGs are cached for subsequent frames

#### Scenario: RGs rebuilt when body count changes

- **WHEN** `GPUStep(cb)` detects that `body_count` differs from the cached snapshot
- **THEN** all solver RGs are rebuilt with updated workgroup counts and buffer sizes

#### Scenario: RGs rebuilt when joint counts change

- **WHEN** `GPUStep(cb)` detects that `hinge_joint_count` or `fixed_joint_count` changed
- **THEN** RGs containing joint-related passes (PostCollisionPreIterRG, PositionIterRG) are rebuilt
- **AND** RGs without joint passes are NOT rebuilt

#### Scenario: Loop counts do not trigger RG rebuild

- **WHEN** `substep_count`, `pos_iters`, or `vel_iters` change in `XpbdConfig`
- **THEN** no RenderGraph is rebuilt
- **AND** the new loop counts are used in the next `GPUStep` call

#### Scenario: BroadPhase Detect called each substep

- **WHEN** `GPUStep` iterates over substeps
- **THEN** `broad_detector->Detect(cb)` is called once per substep
- **AND** the detector internally records its own RG to `cb`

#### Scenario: NarrowPhase Detect called each substep

- **WHEN** `GPUStep` iterates over substeps after broad-phase completes
- **THEN** `narrow_detector->Detect(cb)` is called once per substep
- **AND** the detector internally records its own RG to `cb`

### Requirement: Cross-RG synchronization via prev_access

Every `ImportExternalResource` call in every `Build*RenderGraph()` method SHALL specify a correct `prev_access` reflecting the buffer state left by the most recently recorded RG in the sequence.

For RGs that are recorded exactly once per phase (PreCollisionRG, PostCollisionPreIterRG, PostPositionRG, ModelMatrixRG), `prev_access` SHALL be the precise access type of the buffer's last use in the preceding RG.

For RGs recorded in a loop (PositionIterRG, VelocityIterRG), `prev_access` for mutable buffers SHALL be `{AT::ShaderRandomRead, AT::ShaderRandomWrite}` ("RW") as a conservative upper bound ensuring correctness across re-recording iterations.

#### Scenario: Linear RG uses precise prev_access

- **WHEN** PostCollisionPreIterRG imports `rigid_body_center_world_position`
- **AND** the last access in NarrowPhaseDetect was `{AT::ShaderRandomRead}` (RR)
- **THEN** `ImportExternalResource` is called with `prev_access = {AT::ShaderRandomRead}`

#### Scenario: Loop RG uses conservative prev_access

- **WHEN** PositionIterRG imports `rigid_body_center_world_position`
- **THEN** `ImportExternalResource` is called with `prev_access = {AT::ShaderRandomRead, AT::ShaderRandomWrite}` regardless of the preceding RG's actual last access

#### Scenario: Detector output buffer prev_access

- **WHEN** the solver's PositionIterRG imports narrow-phase collision result buffers
- **THEN** `prev_access` is set to `{AT::ShaderRandomWrite}` since the detector wrote these buffers

### Requirement: Model matrix update runs unconditionally

The ModelMatrixRG SHALL be recorded after the substep loop completes, regardless of whether simulation is enabled. When `IsSimulationEnabled()` is false, all other RGs still dispatch but with effective time_step = 0 (no displacement), ensuring model matrices remain valid for rendering.

#### Scenario: Model matrices written when simulation paused

- **WHEN** `PhysicsScene::IsSimulationEnabled()` returns `false`
- **THEN** ModelMatrixRG is still recorded
- **AND** other solver RGs still dispatch (with time_step = 0, producing no position change)
