## MODIFIED Requirements

### Requirement: DummySolver implements ISolver

`DummySolver` SHALL inherit from `ISolver` and implement all pure virtual methods. It SHALL be defined in `engine/Physics/Solver/DummySolver.h/.cpp`. It SHALL override `PreGPUStep`, `GPUStep` and `GPUCalcModelMatrices`, and SHALL use the default `PostGPUStep` (no-op).

`DummySolver`'s constructor SHALL take `(Rhi::DeviceContext&)` (replacing the former `RenderSystem&`) and store it internally. The solver SHALL access its bound PhysicsScene through `m_bound_scene` (set by `ISolver::OnBindToScene`). It SHALL NOT override `OnBindToScene` — the default implementation is sufficient.

#### Scenario: DummySolver is polymorphic

- **WHEN** registered via `RegisterSolver(scene_id, std::make_unique<DummySolver>(device_context))`
- **AND** `scene_id` maps to an existing scene
- **THEN** `PhysicsSystem::GPUStep(cb)` SHALL correctly dispatch to `DummySolver::GPUStep(cb)`

#### Scenario: DummySolver accesses scene through m_bound_scene

- **WHEN** `DummySolver::GPUStep(cb)` is called
- **THEN** the solver SHALL obtain GPU buffers via `m_bound_scene->GetGpuBuffers()`

#### Scenario: DummySolver uses stored Rhi facilities

- **WHEN** `DummySolver::GPUStep(cb)` is called
- **THEN** the solver SHALL access the device and allocator through the `Rhi::DeviceContext` stored at construction time, not through a method parameter

#### Scenario: DummySolver produces model matrices on request

- **WHEN** `DummySolver::GPUCalcModelMatrices(cb, target)` is called
- **THEN** the solver records a pass that writes model matrices into `target` from the scene's current poses
- **AND** it does not displace any body

### Requirement: DummySolver dispatches compute directly in GPUStep

On each `GPUStep(cb)` call, the solver SHALL:
1. Insert a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) at the start
2. Dispatch the compute shader via `cb.BindComputeStage`, `cb.BindComputeResource`, `cb.DispatchCompute`
3. Use pre-allocated shader pipeline and resource binding (created in `PreGPUStep`)

The compute shader SHALL displace each alive body by `position.z += gravity.z * time_step`. It SHALL NOT write model matrices: model matrix output belongs to `GPUCalcModelMatrices`, which reads the poses the step produced.

`DummySolver::PreGPUStep()` SHALL perform the shader initialization, uniform buffer write, and binding allocation. `DummySolver::GPUStep(cb)` SHALL only dispatch.

#### Scenario: Bodies move downward each frame

- **WHEN** `GPUStep(cb)` is called with 3 rigid bodies, `gravity = (0,0,-9.81)`, `time_step = 0.01`
- **THEN** the push-constant block SHALL contain `vec4(0, 0, -9.81, 0.01)` recorded before the dispatch
- **AND** compute dispatch SHALL be recorded directly to `cb` (no `RenderGraph::RecordAllPasses`)

#### Scenario: No RenderGraph used

- **WHEN** `GPUStep()` is called for any frame
- **THEN** no `RenderGraph`, `RenderGraphBuilder`, or `RenderGraphPass` is created or used
- **AND** `cb.BindComputeStage`, `cb.BindComputeResource`, `cb.DispatchCompute` are called directly

#### Scenario: The step does not write model matrices

- **WHEN** `GPUStep(cb)` is called and `GPUCalcModelMatrices` is not
- **THEN** no dispatch in the recorded frame binds a model matrices buffer as an output
- **AND** the render-owned model matrices buffer is unchanged by the step

### Requirement: DummySolver compute shader

The solver SHALL provide one displacement shader at `engine/Physics/shader/solver/DummySolver/dummy_solver.comp`:
- Binding 0: `readonly buffer RigidBodyAlive`
- Binding 1: `buffer RigidBodyCenterPosition` (read-write)
- Binding 2: `readonly buffer RigidBodyCenterRotation`
- Push-constant block `DummyPush { vec4 gravity_dt; }` (xyz = gravity, w = time_step)
- Workgroup size 64
- Displaces `pos.z += gravity_dt.z * gravity_dt.w`

Model matrix output SHALL be produced by the shared model matrix shader rather than by this shader, so that the mapping from a body's pose to its model matrix exists in exactly one place.

#### Scenario: Shader loaded from SPIR-V

- **WHEN** first initialized via `PreGPUStep`
- **THEN** the displacement shader SHALL be loaded from `ENGINE_PHYSICS_SPIRV_DIR/solver/DummySolver/dummy_solver.comp.spv`

#### Scenario: Model matrix shader is shared with the XPBD solver

- **WHEN** `GPUCalcModelMatrices` is recorded by either solver
- **THEN** both load the same model matrix SPIR-V module
- **AND** neither declares its own model matrix shader
