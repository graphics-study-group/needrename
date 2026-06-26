# Physics Solver Interface

## Purpose

Defines the abstract `ISolver` base class that all GPU physics solvers must implement, the solver registration API on `PhysicsSystem`, and the per-frame `PreGPUStep` → `GPUStep` → `PostGPUStep` dispatch.

## Requirements

### Requirement: ISolver defines three-phase GPU lifecycle

The engine SHALL provide an abstract `ISolver` class in `engine/Physics/Solver/ISolver.h` with the following interface:

- `virtual void PreGPUStep(RenderSystem &system, PhysicsScene &scene)` — called BEFORE `cb.begin()`; CPU-side preparation hook. Default: no-op.
- `virtual void GPUStep(RenderSystem &system, PhysicsScene &scene, vk::CommandBuffer cb) = 0` — called BETWEEN `cb.begin()` and `cb.end()`; lazily builds RG, updates uniforms, records passes to cb
- `virtual void PostGPUStep(RenderSystem &system, PhysicsScene &scene)` — called AFTER `cb.end()` + submit; GPU→CPU readback hook. Default: no-op.
- `bool IsInitialized() const noexcept = 0` — returns true after shaders and compute stages are loaded

The solver SHALL NOT expose its internal `RenderGraph` — callers only interact through `GPUStep(cb)`.

#### Scenario: Solver creates RG on first GPUStep

- **WHEN** `GPUStep()` is called for the first time on a valid PhysicsScene with at least one rigid body
- **THEN** the solver SHALL build its RenderGraph, cache it, and record its passes to `cb`

#### Scenario: Solver skips RG creation when no bodies exist

- **WHEN** `GPUStep()` is called but the PhysicsScene has zero rigid body slots
- **THEN** the solver SHALL NOT create a RenderGraph and SHALL NOT record anything to `cb`

#### Scenario: PreGPUStep and PostGPUStep are no-ops by default

- **WHEN** a solver subclass does not override `PreGPUStep` or `PostGPUStep`
- **THEN** calling these methods SHALL have no effect

### Requirement: PhysicsSystem supports solver registration

`PhysicsSystem` SHALL expose a `RegisterSolver(std::unique_ptr<ISolver>)` method. Registered solvers SHALL be stored in insertion order.

#### Scenario: Multiple solvers registered

- **WHEN** two solvers A and B are registered in that order
- **AND** `PreGPUStep()` is called
- **THEN** `solverA->PreGPUStep()` SHALL be called before `solverB->PreGPUStep()`

### Requirement: PhysicsSystem dispatches three-phase step to all solvers

`PhysicsSystem` SHALL expose `PreGPUStep(RenderSystem&)`, `GPUStep(RenderSystem&, vk::CommandBuffer)`, and `PostGPUStep(RenderSystem&)`. Each SHALL iterate all registered solvers and call the corresponding `ISolver` method on the main scene (`scene_id=0`).

`vk::CommandBuffer` SHALL be forward-declared via `namespace vk { struct CommandBuffer; }` in `PhysicsSystem.h` without including `<vulkan/vulkan.hpp>`.

#### Scenario: PreGPUStep with no solvers

- **WHEN** `PhysicsSystem::PreGPUStep()` is called but no solvers are registered
- **THEN** the method SHALL return immediately with no side effects

#### Scenario: GPUStep dispatches cb to all solvers

- **WHEN** `PhysicsSystem::GPUStep(render_system, cb)` is called with two registered solvers
- **THEN** `solverA->GPUStep(render_system, scene, cb)` SHALL be called first
- **AND** then `solverB->GPUStep(render_system, scene, cb)` SHALL be called
- **AND** each solver SHALL have recorded its passes to `cb` before returning
