# physics-dummy-solver

## MODIFIED Requirements

### Requirement: DummySolver implements ISolver

`DummySolver` SHALL inherit from `ISolver` and implement all pure virtual methods. It SHALL be defined in `engine/Physics/Solver/DummySolver.h/.cpp`. It SHALL implement `GPUStep`, and SHALL rely on the base class for `OnBindToScene`. It SHALL NOT declare a preparation or post-processing phase.

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

#### Scenario: DummySolver declares no preparation phase

- **WHEN** the `DummySolver` class declaration is inspected
- **THEN** it declares no method that a caller must invoke before `GPUStep`
- **AND** a freshly constructed solver records a complete step on its first `GPUStep(cb)`

### Requirement: DummySolver dispatches compute directly in GPUStep

On each `GPUStep(cb)` call, the solver SHALL:
1. Acquire the compute kernel for its shader if it does not hold one yet, and prepare its per-dispatch constants
2. Insert a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) at the start
3. Dispatch the compute shader through the compute kernel dispatch surface

The compute shader SHALL displace each alive body by `position.z += gravity.z * time_step` and write its model matrix.

`DummySolver::GPUStep(cb)` SHALL perform both the initialization and the dispatch. The solver SHALL declare no preparation phase that a caller must invoke before it, because a kernel acquired during recording is safe and the solver's first `GPUStep` must be complete on its own.

#### Scenario: Bodies move downward each frame

- **WHEN** `GPUStep(cb)` is called with 3 rigid bodies, `gravity = (0,0,-9.81)`, `time_step = 0.01`
- **THEN** the push-constant block SHALL contain `vec4(0, 0, -9.81, 0.01)` recorded before the dispatch
- **AND** compute dispatch SHALL be recorded directly to `cb` (no `RenderGraph::RecordAllPasses`)

#### Scenario: No RenderGraph used

- **WHEN** `GPUStep()` is called for any frame
- **THEN** no `RenderGraph`, `RenderGraphBuilder`, or `RenderGraphPass` is created or used
- **AND** the dispatch is recorded directly through the kernel dispatch surface

#### Scenario: Dispatch reuses the kernel acquired earlier

- **WHEN** `GPUStep(cb)` is called
- **THEN** the kernel it dispatches was acquired no later than the start of that call
- **AND** no pipeline or shader module is created between the call's first and last dispatch
- **AND** a second `GPUStep` call acquires nothing

#### Scenario: The first step initializes the solver

- **WHEN** `GPUStep(cb)` is called on a solver that holds no kernel yet
- **THEN** the shader is loaded, the kernel is acquired, and the dispatch is recorded in that same call

### Requirement: DummySolver compute shader

The solver SHALL provide one shader at `engine/Physics/shader/solver/DummySolver/dummy_solver.comp`:
- Binding 0: `readonly buffer RigidBodyAlive`
- Binding 1: `buffer RigidBodyCenterPosition` (read-write)
- Binding 2: `readonly buffer RigidBodyCenterRotation`
- Binding 3: `writeonly buffer ModelMatrices`
- Push-constant block `DummyPush { vec4 gravity_dt; }` (xyz = gravity, w = time_step)
- Workgroup size 64
- Displaces `pos.z += gravity_dt.z * gravity_dt.w`, writes TRS model matrix

#### Scenario: Shader loaded from SPIR-V

- **WHEN** the solver records its first `GPUStep(cb)`
- **THEN** the shader SHALL be loaded from `ENGINE_PHYSICS_SPIRV_DIR/solver/DummySolver/dummy_solver.comp.spv`
