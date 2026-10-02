# xpbd-solver-multi-rg

## MODIFIED Requirements

### Requirement: XpbdGpuSolver inherits ISolver

The system SHALL provide an `XpbdGpuSolver` class inheriting `ISolver`, defined in `engine/Physics/Solver/XpbdGpuSolver.h`. The constructor SHALL accept `(Rhi::DeviceContext&)`. `PhysicsScene` access SHALL be obtained through `m_bound_scene` (set by `OnBindToScene`).

The class SHALL NOT expose any `AddStepPasses()` or `RenderGraphBuilder &` — callers interact only through `GPUStep(cb)`.

```cpp
class XpbdGpuSolver : public ISolver {
public:
    XpbdGpuSolver(Rhi::DeviceContext &device_context);
    ~XpbdGpuSolver() override;

    void GPUStep(vk::CommandBuffer cb) override;
    bool IsInitialized() const noexcept override;

    void SetConfig(const XpbdConfig &config) noexcept;
    const XpbdConfig &GetConfig() const noexcept;
};
```

#### Scenario: Solver registered via PhysicsSystem

- **WHEN** `PhysicsSystem::RegisterSolver(scene_id, std::make_unique<XpbdGpuSolver>(device_context))` is called
- **THEN** `XpbdGpuSolver::OnBindToScene(scene)` is invoked, setting `m_bound_scene`
- **AND** the solver appears in the per-frame `GPUStep` dispatch

#### Scenario: Solver constructor does not allocate GPU resources

- **WHEN** `XpbdGpuSolver` is constructed with a valid `Rhi::DeviceContext`
- **THEN** no compute pipeline, compute buffer, or descriptor resource is created
- **AND** `IsInitialized()` returns `false`

### Requirement: Record-time preparation replaces the pre-step phase

`XpbdGpuSolver::GPUStep(cb)` SHALL perform, during recording, all preparation that the solver needs before and between its dispatches. There SHALL be no solver method that a caller must invoke before `GPUStep` for the step to be correct. The preparation SHALL comprise:

1. Reading GPU buffers from `m_bound_scene`, and returning without recording when no alive bodies exist
2. Lazily acquiring the compute pipelines for every solver pass on first use, and creating the buffers and resource bindings those passes require
3. Sizing every intermediate buffer to the geometry the current step implies (body count, contact capacity, joint counts), reusing an existing buffer whenever its capacity already suffices
4. Preparing the per-dispatch constants (gravity and substep dt, entry counts, grid configuration, contact margin, scan and sort parameters)
5. Preparing the owned collision detectors for the current shape count, including the narrow detector's reference to the broad detector's pair buffers

Every input to this preparation SHALL be a value the CPU already holds at record time; none of it SHALL require a value produced on the GPU.

#### Scenario: First GPUStep acquires pipelines and sizes buffers

- **WHEN** `GPUStep(cb)` is called for the first time on a scene with at least one rigid body
- **THEN** all required SPIR-V files are loaded from `<ENGINE_PHYSICS_SPIRV_DIR>/solver/`
- **AND** the solver's compute pipelines are acquired
- **AND** the buffers the step requires are allocated
- **AND** `IsInitialized()` returns `true` thereafter

#### Scenario: Buffer sizing on body count change happens inside the step

- **WHEN** a `GPUStep` observes a body count different from the previous step's
- **THEN** the intermediate buffers (snapshots, deltas, Lagrange multipliers) are sized to the new geometry during that `GPUStep`
- **AND** buffers whose capacity already suffices are reused rather than recreated
- **AND** buffers for changed joint and contact counts are sized in the same call

#### Scenario: Detector preparation happens inside the step

- **WHEN** a `GPUStep` begins with a shape count different from the previous step's
- **THEN** the broad detector is prepared for that shape count during the step, sizing its internal buffers
- **AND** the solver obtains the broad detector's pair buffers and prepares the narrow detector with them
- **AND** no caller-visible configure call is required between steps

#### Scenario: No preparation is needed before the first GPUStep

- **WHEN** a freshly registered solver's `GPUStep(cb)` is called as the very first call on the solver
- **THEN** the step is recorded completely
- **AND** the caller performed no other solver call in that frame

### Requirement: GPUStep owns compute dispatch recording

`XpbdGpuSolver::GPUStep(vk::CommandBuffer cb)` SHALL record physics compute dispatches directly to `cb` without using `RenderGraph`. The method SHALL:

1. Read GPU buffers from `m_bound_scene`; early-return if no alive bodies
2. Ensure its compute pipelines are acquired and that every buffer, binding and per-dispatch constant the step needs is ready, allocating GPU resources where the current geometry requires it
3. Insert a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) at the start to ensure preceding GPU work is visible
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
   ```

The PreCollision pass order SHALL be: substep-start position/orientation snapshots → integrate forces → pre-contact velocity snapshots → update shape world poses. This ensures pre-contact velocity snapshots capture post-integration velocity (including gravity) for correct restitution reference in the velocity solver.

Model matrix output SHALL NOT be part of `GPUStep`: the step neither records a model matrix pass nor touches the render-owned model matrices buffer, which the caller fills by invoking the model matrix entry point.

`GPUStep` MAY allocate and resize GPU resources. Resources it replaces are retired by the device and released only once the submissions that may reference them have completed, so a resize during recording cannot invalidate in-flight work.

Barrier placement SHALL remain explicit and recorded by the solver. The dispatch interface SHALL NOT insert barriers on the solver's behalf.

#### Scenario: Compute dispatches recorded directly to command buffer

- **WHEN** `GPUStep(cb)` is called with a valid scene and simulation enabled
- **THEN** all solver compute dispatches and detector dispatches are recorded directly to `cb` through the RHI compute dispatch interface
- **AND** no `RenderGraph::RecordAllPasses` or `RenderGraphBuilder::BuildRenderGraph` is called

#### Scenario: Entry barrier inserted at start of each phase

- **WHEN** `GPUStep(cb)` begins a new phase (PreCollision, PostCollisionPreIter, PositionIter, PostPosition, VelocityIter)
- **THEN** a `vk::MemoryBarrier2` is recorded before the first dispatch of that phase
- **AND** the barrier ensures all ShaderStorageWrite from the previous phase is visible

#### Scenario: Loop counts do not trigger any rebuild

- **WHEN** `substep_count`, `pos_iters`, or `vel_iters` change in `XpbdConfig`
- **THEN** the new loop counts are used in the next `GPUStep` call
- **AND** no pipeline or descriptor state is rebuilt for that reason alone

#### Scenario: Geometry growth during a step is safe

- **WHEN** a `GPUStep` replaces a buffer because the current geometry exceeds its capacity
- **THEN** the replaced buffer's storage is not freed while an earlier submission may still reference it
- **AND** the current step records against the replacement
- **AND** no validation error is reported for the replaced handle

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
- **THEN** the step records no model matrix dispatch, because model matrix output is requested separately by the caller
- **AND** other solver passes still dispatch (with time_step = 0, producing no position change)
- **AND** the caller can still produce model matrices for that frame by invoking the model matrix entry point

## RENAMED Requirements

FROM: `PreGPUStep handles CPU-side preparation`
TO: `Record-time preparation replaces the pre-step phase`
