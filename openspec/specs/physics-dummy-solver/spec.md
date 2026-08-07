# physics-dummy-solver

## Purpose

Defines a minimal `DummySolver` that displaces all rigid bodies along `-Z` by a configurable step size and writes model matrices via a compute shader, dispatching compute directly to the command buffer (no RenderGraph). This solver validates the `ISolver` interface and the RenderGraph-free physics GPU architecture.

## Requirements

### Requirement: DummySolver implements ISolver

`DummySolver` SHALL inherit from `ISolver` and implement all pure virtual methods. It SHALL be defined in `engine/Physics/Solver/DummySolver.h/.cpp`. It SHALL override `PreGPUStep` and `GPUStep`, and SHALL use the default `PostGPUStep` (no-op).

`DummySolver`'s constructor SHALL take `RenderSystem&` and store it internally. The solver SHALL access its bound PhysicsScene through `m_bound_scene` (set by `ISolver::OnBindToScene`). It SHALL NOT override `OnBindToScene` — the default implementation is sufficient.

#### Scenario: DummySolver is polymorphic

- **WHEN** registered via `RegisterSolver(scene_id, std::make_unique<DummySolver>(rs))`
- **AND** `scene_id` maps to an existing scene
- **THEN** `PhysicsSystem::GPUStep(cb)` SHALL correctly dispatch to `DummySolver::GPUStep(cb)`

#### Scenario: DummySolver accesses scene through m_bound_scene

- **WHEN** `DummySolver::GPUStep(cb)` is called
- **THEN** the solver SHALL obtain GPU buffers via `m_bound_scene->GetGpuBuffers()`

#### Scenario: DummySolver uses stored RenderSystem

- **WHEN** `DummySolver::GPUStep(cb)` is called
- **THEN** the solver SHALL access the RenderSystem through the reference stored at construction time, not through a method parameter

### Requirement: DummySolver dispatches compute directly in GPUStep

On each `GPUStep(cb)` call, the solver SHALL:
1. Insert a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) at the start
2. Dispatch the compute shader via `cb.BindComputeStage`, `cb.BindComputeResource`, `cb.DispatchCompute`
3. Use pre-allocated shader pipeline and resource binding (created in `PreGPUStep`)

The compute shader SHALL displace each alive body by `position.z += gravity.z * time_step` and write its model matrix.

`DummySolver::PreGPUStep()` SHALL perform the shader initialization, uniform buffer write, and binding allocation. `DummySolver::GPUStep(cb)` SHALL only dispatch.

#### Scenario: Bodies move downward each frame

- **WHEN** `GPUStep(cb)` is called with 3 rigid bodies, `gravity = (0,0,-9.81)`, `time_step = 0.01`
- **THEN** uniform buffer SHALL contain `vec4(0, 0, -9.81, 0.01)`
- **AND** compute dispatch SHALL be recorded directly to `cb` (no `RenderGraph::RecordAllPasses`)

#### Scenario: No RenderGraph used

- **WHEN** `GPUStep()` is called for any frame
- **THEN** no `RenderGraph`, `RenderGraphBuilder`, or `RenderGraphPass` is created or used
- **AND** `cb.BindComputeStage`, `cb.BindComputeResource`, `cb.DispatchCompute` are called directly

### Requirement: DummySolver compute shader

The solver SHALL provide one shader at `engine/Physics/shader/solver/DummySolver/dummy_solver.comp`:
- Binding 0: `readonly buffer RigidBodyAlive`
- Binding 1: `buffer RigidBodyCenterPosition` (read-write)
- Binding 2: `readonly buffer RigidBodyCenterRotation`
- Binding 3: `readonly buffer DummySolverUniforms { vec4 gravity_dt; }`
- Binding 4: `writeonly buffer ModelMatrices`
- Workgroup size 64
- Displaces `pos.z += gravity_dt.z * gravity_dt.w`, writes TRS model matrix

#### Scenario: Shader loaded from SPIR-V

- **WHEN** first initialized via `PreGPUStep`
- **THEN** shader SHALL be loaded from `ENGINE_PHYSICS_SPIRV_DIR/solver/DummySolver/dummy_solver.comp.spv`
