# Physics Dummy Solver

## Purpose

Defines a minimal `DummySolver` that displaces all rigid bodies along `-Z` by a configurable step size and writes model matrices via a compute shader. This solver validates the `ISolver` interface and the separate physics RenderGraph architecture.

## ADDED Requirements

### Requirement: DummySolver implements ISolver

`DummySolver` SHALL inherit from `ISolver` and implement all pure virtual methods. `PreGPUStep` and `PostGPUStep` SHALL use default no-ops. It SHALL be defined in `engine/Physics/Solver/DummySolver.h/.cpp`.

#### Scenario: DummySolver is polymorphic

- **WHEN** registered via `std::unique_ptr<ISolver>` to `PhysicsSystem::RegisterSolver`
- **THEN** `PhysicsSystem::GPUStep()` SHALL correctly dispatch to `DummySolver::GPUStep()`

### Requirement: DummySolver displaces bodies and records RG in GPUStep

On each `GPUStep(system, scene, cb)` call, the solver SHALL:
1. Ensure shaders are loaded (once)
2. Update host-visible uniform buffer with `vec4(gravity.xyz, time_step)`
3. Lazily build RG on first call (or if body count changed)
4. Call `m_rg->RecordAllPasses(cb)` to record the compute pass

The compute shader SHALL displace each alive body by `position.z += gravity.z * time_step` and write its model matrix.

#### Scenario: Bodies move downward each frame

- **WHEN** `GPUStep(system, scene, cb)` is called with 3 rigid bodies, `gravity = (0,0,-9.81)`, `time_step = 0.01`
- **THEN** uniform buffer SHALL contain `vec4(0, 0, -9.81, 0.01)`
- **AND** `m_rg->RecordAllPasses(cb)` SHALL be called after uniform update

#### Scenario: RenderGraph lazily created and reused

- **WHEN** `GPUStep()` is called for the second time with unchanged body count
- **THEN** `BuildRenderGraph()` SHALL NOT be called again
- **AND** the cached RG SHALL be recorded to `cb`

### Requirement: DummySolver imports physics scene buffers once

When building the RG, buffers `rigid_body_alive`, `rigid_body_center_world_position`, `rigid_body_center_world_rotation`, `model_matrices` SHALL each be imported exactly once with `prev_access = None`.

#### Scenario: No duplicate imports

- **WHEN** `BuildRenderGraph()` is called
- **THEN** each required buffer SHALL be imported exactly once

### Requirement: DummySolver compute shader

One shader at `engine/Physics/shader/solver/DummySolver/dummy_solver.comp`:
- Binding 0: `readonly buffer RigidBodyAlive`
- Binding 1: `buffer RigidBodyCenterPosition` (read-write)
- Binding 2: `readonly buffer RigidBodyCenterRotation`
- Binding 3: `readonly buffer DummySolverUniforms { vec4 gravity_dt; }`
- Binding 4: `writeonly buffer ModelMatrices`
- Workgroup size 64
- Displaces `pos.z += gravity_dt.z * gravity_dt.w`, writes TRS model matrix

#### Scenario: Shader loaded from SPIR-V

- **WHEN** first initialized
- **THEN** shader SHALL be loaded from `ENGINE_PHYSICS_SPIRV_DIR/solver/DummySolver/dummy_solver.comp.spv`
