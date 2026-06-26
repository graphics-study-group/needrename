## Why

The current `ISolver` interface and `PhysicsSystem` solver management have three architectural flaws: (1) `PhysicsScene` is passed as a parameter to every `ISolver` method instead of being bound at registration time, (2) the three `PhysicsSystem::Step` functions hardcode `GetScenePtr(1)`, making multi-scene physics impossible, and (3) `RenderSystem&` is redundantly passed to every `ISolver` method despite each solver already storing it at construction time. This prevents proper multi-scene support and creates unnecessary coupling between `PhysicsSystem` and the solver dispatch logic.

## What Changes

- **BREAKING**: Remove `RenderSystem&` and `PhysicsScene&` from all three `ISolver` virtual method signatures
- **BREAKING**: Add `OnBindToScene(PhysicsScene&)` to `ISolver` — called by `PhysicsSystem` at registration time
- **BREAKING**: Add `scene_id` parameter to `PhysicsSystem::RegisterSolver`, changing its signature from `RegisterSolver(unique_ptr<ISolver>)` to `RegisterSolver(uint32_t scene_id, unique_ptr<ISolver>)`
- **BREAKING**: Change `PhysicsSystem` step methods to iterate all scenes and their bound solvers, removing the hardcoded `scene_id=1` assumption
- Add protected `m_bound_scene` member to `ISolver` base class for derived solver access
- Update `DummySolver` to use `m_bound_scene` and the stored `RenderSystem&` instead of method parameters
- Forward-declare `PhysicsScene` in `ISolver.h` instead of relying on `PhysicsSystem` to pass it

## Capabilities

### New Capabilities
<!-- No new capabilities — this is a refactoring of existing interfaces -->

### Modified Capabilities
- `physics-solver-interface`: Change ISolver method signatures to remove RenderSystem& and PhysicsScene&; add OnBindToScene binding mechanism; change PhysicsSystem step dispatch to be scene-aware
- `physics-dummy-solver`: Update DummySolver implementation to match the new ISolver interface (use m_bound_scene, stored RenderSystem&)
- `physics-render-graph-separation`: Update requirement descriptions that reference the old ISolver GPUStep signature (RenderSystem&, PhysicsScene&, CommandBuffer → just CommandBuffer)

## Impact

- **ISolver.h**: Method signatures change; add `OnBindToScene` virtual; add protected `m_bound_scene`
- **PhysicsSystem.h/.cpp**: `RegisterSolver` takes `scene_id`; m_solvers becomes `map<id, vector<ISolver>>`; step methods iterate all scenes; remove hardcoded `GetScenePtr(1)`
- **DummySolver.h/.cpp**: Update overrides to new signatures; use `m_bound_scene` and stored `RenderSystem&`
- **MainClass.cpp** (engine): `physics->PreGPUStep(*renderer)` becomes `physics->PreGPUStep()`; same for GPUStep/PostGPUStep
- **physics_example/main.cpp**: `RegisterSolver(std::move(solver))` becomes `RegisterSolver(scene_id, std::move(solver))`
