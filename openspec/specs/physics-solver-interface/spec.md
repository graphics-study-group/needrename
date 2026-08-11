# Physics Solver Interface

## Purpose

Defines the abstract `ISolver` base class that all GPU physics solvers must implement, the solver registration API on `PhysicsSystem`, and the per-frame `PreGPUStep` → `GPUStep` → `PostGPUStep` dispatch.

## Requirements

### Requirement: ISolver defines three-phase GPU lifecycle

The engine SHALL provide an abstract `ISolver` class in `engine/Physics/Solver/ISolver.h` with the following interface:

- `virtual void OnBindToScene(PhysicsScene &scene)` — called by `PhysicsSystem::RegisterSolver` to bind this solver to a specific scene. Default implementation sets `m_bound_scene = &scene`.
- `virtual void PreGPUStep()` — called BEFORE `cb.begin()` each frame; CPU-side preparation hook. Default: no-op.
- `virtual void GPUStep(vk::CommandBuffer cb) = 0` — called BETWEEN `cb.begin()` and `cb.end()`; lazily builds RG, updates uniforms, records passes to cb. The solver SHALL access its bound scene via `m_bound_scene`.
- `virtual void PostGPUStep()` — called AFTER `cb.end()` + submit; GPU-to-CPU readback hook. Default: no-op.
- `bool IsInitialized() const noexcept = 0` — returns true after shaders and compute stages are loaded.
- `PhysicsScene *m_bound_scene` — protected member set by `OnBindToScene`, accessible to derived classes to obtain GPU buffers and scene state.

`ISolver` SHALL forward-declare `PhysicsScene` (`class PhysicsScene;`) without including its header. `RenderSystem&` and `PhysicsScene&` SHALL NOT appear in method parameters — solvers access these through their stored references (constructor or `m_bound_scene`).

`vk::CommandBuffer` SHALL be forward-declared via `namespace vk { struct CommandBuffer; }`.

The solver SHALL NOT expose its internal `RenderGraph` — callers only interact through `GPUStep(cb)`.

#### Scenario: Solver creates RG on first GPUStep

- **WHEN** `GPUStep()` is called for the first time on a valid PhysicsScene with at least one rigid body
- **AND** `m_bound_scene` is valid (set by prior `OnBindToScene`)
- **THEN** the solver SHALL build its RenderGraph, cache it, and record its passes to `cb`

#### Scenario: Solver skips RG creation when no bodies exist

- **WHEN** `GPUStep()` is called but the bound PhysicsScene has zero rigid body slots
- **THEN** the solver SHALL NOT create a RenderGraph and SHALL NOT record anything to `cb`

#### Scenario: PreGPUStep and PostGPUStep are no-ops by default

- **WHEN** a solver subclass does not override `PreGPUStep` or `PostGPUStep`
- **THEN** calling these methods SHALL have no effect

#### Scenario: OnBindToScene sets m_bound_scene by default

- **WHEN** `OnBindToScene(scene)` is called via the default implementation
- **THEN** `m_bound_scene` SHALL point to `&scene`

### Requirement: PhysicsSystem supports per-scene solver registration

`PhysicsSystem` SHALL expose a `RegisterSolver(uint32_t scene_id, std::unique_ptr<ISolver>)` method. Upon registration:

1. If `scene_id` does not exist in `m_scene_map`, the solver SHALL NOT be registered and a warning SHALL be logged.
2. If the scene exists, `solver->OnBindToScene(*scene)` SHALL be called to bind the solver to the scene.
3. The solver SHALL then be stored in insertion order for that scene.

Solver storage backing SHALL be a mapping from `scene_id` to an ordered container of `unique_ptr<ISolver>`.

#### Scenario: Solver registered to existing scene

- **WHEN** `RegisterSolver(5, solver)` is called and scene 5 exists
- **THEN** `solver->OnBindToScene(*scene5)` SHALL be called
- **AND** `solver` SHALL be stored for scene 5

#### Scenario: Solver registered to non-existent scene

- **WHEN** `RegisterSolver(99, solver)` is called but scene 99 does not exist
- **THEN** a warning SHALL be logged
- **AND** `solver` SHALL NOT be stored

#### Scenario: Multiple solvers registered to same scene

- **WHEN** solvers A and B are registered to scene 1 in that order
- **AND** `PreGPUStep()` is called
- **THEN** `solverA->PreGPUStep()` SHALL be called before `solverB->PreGPUStep()`

### Requirement: PhysicsSystem dispatches three-phase step to all scenes and solvers

`PhysicsSystem` SHALL expose `PreGPUStep()`, `GPUStep(vk::CommandBuffer)`, and `PostGPUStep()`. Each SHALL iterate all scenes and their registered solvers, calling the corresponding `ISolver` method. Scenes with no registered solvers SHALL be skipped.

`vk::CommandBuffer` SHALL be forward-declared via `namespace vk { struct CommandBuffer; }` in `PhysicsSystem.h` without including `<vulkan/vulkan.hpp>`.

#### Scenario: PreGPUStep with no solvers

- **WHEN** `PhysicsSystem::PreGPUStep()` is called but no solvers are registered anywhere
- **THEN** the method SHALL return immediately with no side effects

#### Scenario: GPUStep dispatches to all scenes

- **WHEN** `PhysicsSystem::GPUStep(cb)` is called with scene 1 having solver A and scene 2 having solver B
- **THEN** `solverA->GPUStep(cb)` SHALL be called for scene 1
- **AND** `solverB->GPUStep(cb)` SHALL be called for scene 2
- **AND** each solver SHALL have recorded its passes to `cb` before returning

#### Scenario: Scene with no solvers is skipped

- **WHEN** `PhysicsSystem::GPUStep(cb)` is called and scene 3 exists but has no registered solvers
- **THEN** no `GPUStep` call SHALL be made for scene 3

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
