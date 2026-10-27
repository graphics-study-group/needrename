## ADDED Requirements

### Requirement: PhysicsSystem produces model matrices for one scene

`PhysicsSystem` SHALL expose an entry point that produces model matrices for a single named physics scene, taking the command buffer and the target buffer as parameters, and SHALL forward it to every solver registered for that scene in registration order.

The entry point SHALL be scoped to one scene: a caller that renders one scene's rigid bodies SHALL NOT cause any other scene's solvers to write into the same target buffer. Scenes with no registered solvers SHALL be skipped.

The target buffer SHALL be supplied by the caller for the duration of the call only. Neither `PhysicsSystem` nor any solver SHALL retain a reference to it between calls.

#### Scenario: The call reaches the scene's solvers

- **WHEN** the entry point is called for scene 1 with solver A registered, and the target buffer supplied
- **THEN** `solverA->GPUCalcModelMatrices(cb, target)` is called
- **AND** the target buffer is not retained after the call returns

#### Scenario: Another scene's solvers are not invoked

- **WHEN** the entry point is called for scene 1 and scene 2 also has registered solvers
- **THEN** no solver of scene 2 receives the call
- **AND** the supplied target buffer is written only by scene 1's solvers

#### Scenario: A scene with no solvers is skipped

- **WHEN** the entry point is called for a scene that has no registered solvers
- **THEN** the call has no effect
- **AND** the target buffer is left untouched

## MODIFIED Requirements

### Requirement: ISolver defines three-phase GPU lifecycle

The engine SHALL provide an abstract `ISolver` class in `engine/Physics/Solver/ISolver.h` with the following interface:

- `virtual void OnBindToScene(PhysicsScene &scene)` — called by `PhysicsSystem::RegisterSolver` to bind this solver to a specific scene. Default implementation sets `m_bound_scene = &scene`.
- `virtual void PreGPUStep()` — called BEFORE `cb.begin()` each frame; CPU-side preparation hook. Default: no-op.
- `virtual void GPUStep(vk::CommandBuffer cb) = 0` — called BETWEEN `cb.begin()` and `cb.end()`; lazily builds RG, updates uniforms, records passes to cb. The solver SHALL access its bound scene via `m_bound_scene`.
- `virtual void PostGPUStep()` — called AFTER `cb.end()` + submit; GPU-to-CPU readback hook. Default: no-op.
- `virtual void GPUCalcModelMatrices(vk::CommandBuffer cb, Rhi::ComputeBuffer &target) = 0` — records a pass that writes model matrices into `target` from the solver's current scene state. Pure virtual, so every concrete solver provides an implementation rather than inheriting a shared default. Invoked by the caller, not by `GPUStep`, and only when the caller has a target to write into.
- `bool IsInitialized() const noexcept = 0` — returns true after shaders and compute stages are loaded.
- `PhysicsScene *m_bound_scene` — protected member set by `OnBindToScene`, accessible to derived classes to obtain GPU buffers and scene state.

`ISolver` SHALL forward-declare `PhysicsScene` (`class PhysicsScene;`) without including its header. `RenderSystem&` and `PhysicsScene&` SHALL NOT appear in method parameters — solvers access these through their stored references (constructor or `m_bound_scene`).

`Rhi::ComputeBuffer` SHALL be forward-declared without including its header, so that the physics module keeps no include dependency on a Render or Framework header.

`vk::CommandBuffer` SHALL be forward-declared via `namespace vk { struct CommandBuffer; }`.

The solver SHALL NOT expose its internal `RenderGraph` — callers only interact through `GPUStep(cb)`.

`GPUCalcModelMatrices` SHALL be independent of `GPUStep`: a caller SHALL be able to produce model matrices without running a step, and running a step SHALL NOT itself produce model matrices. The implementation SHALL record its own barrier before its dispatch, because it knows which earlier writes it reads.

The caller SHALL ensure that `target` is large enough for the scene's rigid body slot count before the call. An implementation SHALL NOT write beyond the target's capacity; it SHALL clamp its dispatch to it and SHALL assert in debug builds when the requested element count exceeds the capacity.

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

#### Scenario: Model matrices are produced without a step

- **WHEN** `GPUCalcModelMatrices(cb, target)` is called on a scene whose poses are already uploaded, and no `GPUStep` is called for that frame
- **THEN** the solver records a pass that writes model matrices derived from the current poses into `target`
- **AND** no physics integration is performed

#### Scenario: Model matrices are not produced by a step

- **WHEN** `GPUStep(cb)` is called and `GPUCalcModelMatrices` is not
- **THEN** the solver does not write any model matrices
- **AND** the target buffer's contents are unchanged

#### Scenario: The model matrix entry point is pure virtual

- **WHEN** a concrete solver class is declared
- **THEN** it provides `GPUCalcModelMatrices`, because `ISolver` declares that entry point pure virtual
- **AND** a solver that produces no model matrices still provides the implementation itself — a no-op is that solver's choice, not a default the interface supplies

#### Scenario: An undersized target is clamped

- **WHEN** `GPUCalcModelMatrices` is called with a target whose capacity is smaller than the scene's rigid body slot count
- **THEN** no write occurs beyond the target's capacity
- **AND** the condition is reported in debug builds

### Requirement: XpbdGpuSolver implements ISolver

The `XpbdGpuSolver` class SHALL inherit `ISolver` and implement the three-phase GPU lifecycle. Its constructor SHALL accept `(Rhi::DeviceContext&)` — replacing the former `RenderSystem &` — and the `PhysicsScene` reference SHALL be obtained through `m_bound_scene` (set by the inherited `OnBindToScene`). Solver GPU facilities SHALL come from the stored `Rhi::DeviceContext`; `Rhi::SubmissionHelper` SHALL be used for uploads where needed.

The solver SHALL own multiple RenderGraphs internally, one per distinct physics phase. These RGs SHALL NOT be exposed to callers — interaction is only through `GPUStep(cb)`.

`PreGPUStep()` SHALL load shaders lazily, ensure intermediate buffers are sized, upload uniform data, and call `Configure` on owned collision detectors.

`GPUStep(cb)` SHALL lazily build all owned RGs, then record them in sequence with substep/iteration loops on the CPU side.

`IsInitialized()` SHALL return `true` after the first `PreGPUStep` successfully loads all shaders.

`XpbdGpuSolver` SHALL NOT call `SceneDataManager::SetModelMatricesBuffer`, and SHALL NOT read a model matrices buffer from its bound scene. It SHALL write model matrices only into the buffer passed to `GPUCalcModelMatrices`.

#### Scenario: XpbdGpuSolver registered via PhysicsSystem

- **WHEN** `RegisterSolver(scene_id, std::make_unique<XpbdGpuSolver>(device_context))` is called
- **THEN** `OnBindToScene(scene)` sets `m_bound_scene`
- **AND** the solver participates in the per-frame `PreGPUStep` → `GPUStep` dispatch

#### Scenario: Solver no longer touches render data

- **WHEN** `GPUStep(cb)` runs and the scene has rigid bodies
- **THEN** no call is made to any `SceneDataManager` or other Render type
- **AND** the scene's GPU buffer set contains no model matrices buffer

#### Scenario: Model matrix pass reads only solver inputs and writes only the target

- **WHEN** `GPUCalcModelMatrices(cb, target)` records its pass
- **THEN** it binds the scene's rigid body pose buffers as inputs and `target` as its only output
- **AND** it records a barrier before its dispatch
