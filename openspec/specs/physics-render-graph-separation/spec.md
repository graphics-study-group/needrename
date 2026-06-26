# Physics Render Graph Separation

## Purpose

Defines the mechanism by which physics solvers own independent `RenderGraph` instances and record their passes into a shared `vk::CommandBuffer` alongside the rendering `RenderGraph`. Physics RGs are private to their solvers — solvers record their passes directly when `GPUStep(cb)` is called.

## Requirements

### Requirement: RecordAllPasses does not manage CommandBuffer lifecycle

`RenderGraph::RecordAllPasses(vk::CommandBuffer cb)` SHALL record all compiled passes WITHOUT calling `cb.begin()` or `cb.end()`. The caller is responsible for the CB lifecycle.

#### Scenario: RecordAllPasses without begin/end

- **WHEN** `RecordAllPasses(cb)` is called with 3 compiled passes and CB in Recording state
- **THEN** `cb.begin()` and `cb.end()` SHALL NOT be called inside RecordAllPasses
- **AND** all passes' barriers and functions SHALL be recorded

### Requirement: RenderGraph::Execute manages its own CommandBuffer lifecycle

`RenderGraph::Execute(RenderSystem &)` SHALL call `cb.begin()`, `RecordAllPasses(cb)`, `cb.end()`, `SubmitMainCommandBuffer()`. Behavior unchanged from pre-modification.

#### Scenario: Execute unchanged

- **WHEN** a non-physics example calls `render_graph->Execute(system)`
- **THEN** behavior SHALL be identical to before this change

### Requirement: Solver GPUStep records its RenderGraph internally

Each solver's `GPUStep(RenderSystem&, PhysicsScene&, vk::CommandBuffer cb)` implementation SHALL call `m_rg->RecordAllPasses(cb)` after updating per-frame uniforms and ensuring the RG exists. The caller (PhysicsSystem) SHALL NOT access the solver's RG handle.

#### Scenario: Solver records RG to shared CB

- **WHEN** `physics->GPUStep(renderer, cb)` is called between `cb.begin()` and `cb.end()`
- **THEN** each solver with a valid RG SHALL record its passes to `cb`
- **AND** the CB SHALL contain physics compute barriers + pass functions

### Requirement: Multiple RenderGraphs record to a single CommandBuffer

It SHALL be valid to record passes from multiple `RenderGraph` instances onto the same `vk::CommandBuffer` in a single `begin()`/`end()` cycle, provided `prev_access` is correctly set on shared external resources.

#### Scenario: Solver RG + rendering RG on one CB

- **WHEN** `physics->GPUStep(renderer, cb)` records solver passes, followed by `rendering_rg->RecordAllPasses(cb)`
- **AND** the rendering RG imports shared buffers with correct `prev_access`
- **THEN** barriers SHALL correctly transition from physics compute writes to rendering graphics reads

### Requirement: External resource prev_access propagates state across RenderGraph boundaries

The rendering RenderGraph SHALL import shared buffers with `prev_access` reflecting the physics RG's output access type (e.g., `ShaderRandomWrite` for model matrices).

#### Scenario: Rendering RG imports buffer with physics output state

- **WHEN** physics RG writes `model_matrices` with `ShaderRandomWrite`
- **AND** rendering RG imports it with `prev_access = ShaderRandomWrite`
- **AND** the first rendering pass reads it with `ShaderRandomRead`
- **THEN** a barrier from `COMPUTE_SHADER | SHADER_STORAGE_WRITE` to the rendering pass SHALL be inserted
