# Physics Dummy Solver

## Purpose

Defines a minimal `DummySolver` that displaces all rigid bodies along `-Z` by a configurable step size and writes model matrices via a compute shader, dispatching compute directly to the command buffer (no RenderGraph). This solver validates the `ISolver` interface and the RenderGraph-free physics GPU architecture.

## MODIFIED Requirements

### Requirement: DummySolver implements ISolver

`DummySolver` SHALL inherit from `ISolver` and implement all pure virtual methods. It SHALL be defined in `engine/Physics/Solver/DummySolver.h/.cpp`. It SHALL override `PreGPUStep` and `GPUStep`, and SHALL use the default `PostGPUStep` (no-op).

`DummySolver`'s constructor SHALL take `(const Rhi::DeviceInterface&, const Rhi::AllocatorState&)` (replacing the former `RenderSystem&`) and store them internally. The solver SHALL access its bound PhysicsScene through `m_bound_scene` (set by `ISolver::OnBindToScene`). It SHALL NOT override `OnBindToScene` — the default implementation is sufficient.

#### Scenario: DummySolver is polymorphic

- **WHEN** registered via `RegisterSolver(scene_id, std::make_unique<DummySolver>(device_interface, allocator))`
- **AND** `scene_id` maps to an existing scene
- **THEN** `PhysicsSystem::GPUStep(cb)` SHALL correctly dispatch to `DummySolver::GPUStep(cb)`

#### Scenario: DummySolver accesses scene through m_bound_scene

- **WHEN** `DummySolver::GPUStep(cb)` is called
- **THEN** the solver SHALL obtain GPU buffers via `m_bound_scene->GetGpuBuffers()`

#### Scenario: DummySolver uses stored Rhi facilities

- **WHEN** `DummySolver::GPUStep(cb)` is called
- **THEN** the solver SHALL access the device and allocator through the references stored at construction time, not through a method parameter
