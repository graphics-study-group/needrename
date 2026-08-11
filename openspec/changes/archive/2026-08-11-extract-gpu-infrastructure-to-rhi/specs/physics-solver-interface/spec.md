# Physics Solver Interface

## Purpose

Defines the abstract `ISolver` base class that all GPU physics solvers must implement, the solver registration API on `PhysicsSystem`, and the per-frame `PreGPUStep` → `GPUStep` → `PostGPUStep` dispatch.

## MODIFIED Requirements

### Requirement: XpbdGpuSolver implements ISolver

The `XpbdGpuSolver` class SHALL inherit `ISolver` and implement the three-phase GPU lifecycle. Its constructor SHALL accept `(const Rhi::DeviceInterface&, const Rhi::AllocatorState&)` — replacing the former `RenderSystem &` — and the `PhysicsScene` reference SHALL be obtained through `m_bound_scene` (set by the inherited `OnBindToScene`). Solver GPU facilities (device, allocator) SHALL come from the stored `Rhi` references; `Rhi::SubmissionHelper` SHALL be used for uploads where needed.

The solver SHALL own multiple RenderGraphs internally, one per distinct physics phase. These RGs SHALL NOT be exposed to callers — interaction is only through `GPUStep(cb)`.

`PreGPUStep()` SHALL load shaders lazily, ensure intermediate buffers are sized, upload uniform data, and call `Configure` on owned collision detectors.

`GPUStep(cb)` SHALL lazily build all owned RGs, then record them in sequence with substep/iteration loops on the CPU side.

`IsInitialized()` SHALL return `true` after the first `PreGPUStep` successfully loads all shaders.

`XpbdGpuSolver` SHALL NOT call `SceneDataManager::SetModelMatricesBuffer` — the model matrices buffer is forwarded by the `MainClass` assembly layer instead.

#### Scenario: XpbdGpuSolver registered via PhysicsSystem

- **WHEN** `RegisterSolver(scene_id, std::make_unique<XpbdGpuSolver>(device_interface, allocator))` is called
- **THEN** `OnBindToScene(scene)` sets `m_bound_scene`
- **AND** the solver participates in the per-frame `PreGPUStep` → `GPUStep` dispatch

#### Scenario: Solver no longer touches render data

- **WHEN** `GPUStep(cb)` runs and the scene has rigid bodies
- **THEN** no call is made to any `SceneDataManager` or other Render type
- **AND** the model matrices buffer remains available via `m_bound_scene->GetGpuBuffers()`
