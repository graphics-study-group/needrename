# Physics Dummy Solver — Delta Spec

## MODIFIED Requirements

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

### Requirement: DummySolver displaces bodies and records RG in GPUStep

On each `GPUStep(cb)` call, the solver SHALL:
1. Ensure shaders are loaded (once)
2. Update host-visible uniform buffer with `vec4(gravity.xyz, time_step)`
3. Lazily build RG on first call (or if body count changed)
4. Call `m_rg->RecordAllPasses(cb)` to record the compute pass

The compute shader SHALL displace each alive body by `position.z += gravity.z * time_step` and write its model matrix.

`DummySolver::PreGPUStep()` SHALL perform the shader initialization and uniform buffer write (steps 1–2). `DummySolver::GPUStep(cb)` SHALL perform the RG build and pass recording (steps 3–4).

#### Scenario: Bodies move downward each frame

- **WHEN** `GPUStep(cb)` is called with 3 rigid bodies, `gravity = (0,0,-9.81)`, `time_step = 0.01`
- **THEN** uniform buffer SHALL contain `vec4(0, 0, -9.81, 0.01)`
- **AND** `m_rg->RecordAllPasses(cb)` SHALL be called after uniform update

#### Scenario: RenderGraph lazily created and reused

- **WHEN** `GPUStep()` is called for the second time with unchanged body count
- **THEN** `BuildRenderGraph()` SHALL NOT be called again
- **AND** the cached RG SHALL be recorded to `cb`

### Requirement: DummySolver imports physics scene buffers once

When building the RG via `BuildRenderGraph()`, the solver SHALL access buffers through `m_bound_scene->GetGpuBuffers()` and import each required buffer exactly once with `prev_access = None`.

#### Scenario: No duplicate imports

- **WHEN** `BuildRenderGraph()` is called
- **THEN** each required buffer SHALL be imported exactly once
