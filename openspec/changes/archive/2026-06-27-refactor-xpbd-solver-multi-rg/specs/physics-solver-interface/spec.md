# physics-solver-interface

## ADDED Requirements

### Requirement: XpbdGpuSolver implements ISolver

The `XpbdGpuSolver` class SHALL inherit `ISolver` and implement the three-phase GPU lifecycle. Its constructor SHALL accept `RenderSystem &` only — the `PhysicsScene` reference SHALL be obtained through `m_bound_scene` (set by the inherited `OnBindToScene`).

The solver SHALL own multiple RenderGraphs internally, one per distinct physics phase. These RGs SHALL NOT be exposed to callers — interaction is only through `GPUStep(cb)`.

`PreGPUStep()` SHALL load shaders lazily, ensure intermediate buffers are sized, upload uniform data, and call `Configure` on owned collision detectors.

`GPUStep(cb)` SHALL lazily build all owned RGs, then record them in sequence with substep/iteration loops on the CPU side.

`IsInitialized()` SHALL return `true` after the first `PreGPUStep` successfully loads all shaders.

#### Scenario: XpbdGpuSolver registered via PhysicsSystem

- **WHEN** `RegisterSolver(scene_id, std::make_unique<XpbdGpuSolver>(rs))` is called
- **THEN** `OnBindToScene(scene)` sets `m_bound_scene`
- **AND** the solver participates in the per-frame `PreGPUStep` → `GPUStep` dispatch
