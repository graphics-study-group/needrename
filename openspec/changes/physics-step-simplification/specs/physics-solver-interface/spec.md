# physics-solver-interface

## MODIFIED Requirements

### Requirement: ISolver defines the GPU step lifecycle

The engine SHALL provide an abstract `ISolver` class in `engine/Physics/Solver/ISolver.h` with the following interface:

- `virtual void OnBindToScene(PhysicsScene &scene)` — called by `PhysicsSystem::RegisterSolver` to bind this solver to a specific scene. Default implementation sets `m_bound_scene = &scene`.
- `virtual void GPUStep(vk::CommandBuffer cb) = 0` — called BETWEEN `cb.begin()` and `cb.end()`; records the solver's compute dispatches to `cb`, performing on the spot any preparation it needs. The solver SHALL access its bound scene via `m_bound_scene`.
- `virtual void GPUCalcModelMatrices(vk::CommandBuffer cb, Rhi::ComputeBuffer &target) = 0` — records a pass that writes model matrices into `target` from the solver's current scene state. Pure virtual, so every concrete solver provides an implementation rather than inheriting a shared default. Invoked by the caller, not by `GPUStep`, and only when the caller has a target to write into; the caller ensures the target's capacity.
- `bool IsInitialized() const noexcept = 0` — returns true once the solver holds the pipelines and buffers it needs to record a step.
- `PhysicsScene *m_bound_scene` — protected member set by `OnBindToScene`, accessible to derived classes to obtain GPU buffers and scene state.

A solver SHALL NOT require a lifecycle phase outside command-buffer recording. Resource initialization, buffer sizing and per-dispatch constant preparation SHALL occur within `GPUStep`, including on the first call, because resources allocated or resized while a command buffer is being recorded are retired safely by the device. `GPUCalcModelMatrices` is likewise recorded on the caller's command buffer, and running a step SHALL NOT itself produce model matrices.

`ISolver` SHALL forward-declare `PhysicsScene` (`class PhysicsScene;`) and `Rhi::ComputeBuffer` (`class ComputeBuffer;`) without including their headers. `RenderSystem&` and `PhysicsScene&` SHALL NOT appear in method parameters — solvers access these through their stored references (constructor or `m_bound_scene`).

`vk::CommandBuffer` SHALL be forward-declared via `namespace vk { struct CommandBuffer; }`.

The solver SHALL NOT expose its internal recording structure — callers only interact through `GPUStep(cb)`.

#### Scenario: Solver initializes and records on its first GPUStep

- **WHEN** `GPUStep()` is called for the first time on a valid PhysicsScene with at least one rigid body
- **AND** `m_bound_scene` is valid (set by prior `OnBindToScene`)
- **THEN** the solver SHALL acquire its compute pipelines, size its buffers, and record its dispatches to `cb`
- **AND** subsequent calls SHALL reuse the acquired pipelines

#### Scenario: Solver records nothing when no bodies exist

- **WHEN** `GPUStep()` is called but the bound PhysicsScene has zero rigid body slots
- **THEN** the solver SHALL NOT record anything to `cb`

#### Scenario: OnBindToScene sets m_bound_scene by default

- **WHEN** `OnBindToScene(scene)` is called via the default implementation
- **THEN** `m_bound_scene` SHALL point to `&scene`

#### Scenario: No pre-recording phase is required

- **WHEN** a solver is registered and the frame loop calls `GPUStep(cb)` without any earlier per-frame call on the solver
- **THEN** the solver still records a complete step
- **AND** the interface exposes no method that must run before `GPUStep`

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
- **AND** `GPUStep(cb)` is called
- **THEN** `solverA->GPUStep(cb)` SHALL be called before `solverB->GPUStep(cb)`

### Requirement: PhysicsSystem dispatches the step to all scenes and solvers

`PhysicsSystem` SHALL expose `GPUStep(vk::CommandBuffer)`. It SHALL iterate all scenes and their registered solvers, calling the corresponding `ISolver` method. Scenes with no registered solvers SHALL be skipped.

`vk::CommandBuffer` SHALL be forward-declared via `namespace vk { struct CommandBuffer; }` in `PhysicsSystem.h` without including `<vulkan/vulkan.hpp>`.

#### Scenario: Step with no solvers

- **WHEN** `PhysicsSystem::GPUStep(cb)` is called but no solvers are registered anywhere
- **THEN** the method SHALL return immediately with no side effects

#### Scenario: GPUStep dispatches to all scenes

- **WHEN** `PhysicsSystem::GPUStep(cb)` is called with scene 1 having solver A and scene 2 having solver B
- **THEN** `solverA->GPUStep(cb)` SHALL be called for scene 1
- **AND** `solverB->GPUStep(cb)` SHALL be called for scene 2
- **AND** each solver SHALL have recorded its dispatches to `cb` before returning

#### Scenario: Scene with no solvers is skipped

- **WHEN** `PhysicsSystem::GPUStep(cb)` is called and scene 3 exists but has no registered solvers
- **THEN** no `GPUStep` call SHALL be made for scene 3

### Requirement: XpbdGpuSolver implements ISolver

The `XpbdGpuSolver` class SHALL inherit `ISolver` and implement the GPU step lifecycle. Its constructor SHALL accept `(Rhi::DeviceContext&)` — replacing the former `RenderSystem &` — and the `PhysicsScene` reference SHALL be obtained through `m_bound_scene` (set by the inherited `OnBindToScene`). Solver GPU facilities SHALL come from the stored `Rhi::DeviceContext`; `Rhi::SubmissionHelper` SHALL be used for uploads where needed.

`GPUStep(cb)` SHALL lazily acquire the solver's compute pipelines, size the buffers its geometry requires, prepare its per-dispatch constants, prepare its owned collision detectors for the current shape count, and record its dispatches in sequence with substep/iteration loops on the CPU side.

`IsInitialized()` SHALL return `true` once the solver holds its compute pipelines and sized buffers. Because that acquisition happens on the first `GPUStep`, it MAY return `false` before the first step.

`XpbdGpuSolver` SHALL NOT call `SceneDataManager::SetModelMatricesBuffer`, and SHALL NOT read a model matrices buffer from its bound scene. It SHALL write model matrices only into the buffer passed to `GPUCalcModelMatrices`.

#### Scenario: XpbdGpuSolver registered via PhysicsSystem

- **WHEN** `RegisterSolver(scene_id, std::make_unique<XpbdGpuSolver>(device_context))` is called
- **THEN** `OnBindToScene(scene)` sets `m_bound_scene`
- **AND** the solver participates in the per-frame `GPUStep` dispatch

#### Scenario: Solver no longer touches render data

- **WHEN** `GPUStep(cb)` runs and the scene has rigid bodies
- **THEN** no call is made to any `SceneDataManager` or other Render type
- **AND** the scene's GPU buffer set contains no model matrices buffer, because that buffer is owned by the render system

#### Scenario: Geometry change between steps is absorbed inside the step

- **WHEN** the bound scene's body, shape or joint count differs from the previous `GPUStep`
- **THEN** the solver sizes the affected buffers during that `GPUStep`
- **AND** no caller-visible preparation call is needed between the two steps

## RENAMED Requirements

FROM: `ISolver defines three-phase GPU lifecycle`
TO: `ISolver defines the GPU step lifecycle`

FROM: `PhysicsSystem dispatches three-phase step to all scenes and solvers`
TO: `PhysicsSystem dispatches the step to all scenes and solvers`
