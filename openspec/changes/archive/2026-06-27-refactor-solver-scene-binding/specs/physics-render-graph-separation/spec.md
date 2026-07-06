# Physics Render Graph Separation — Delta Spec

## MODIFIED Requirements

### Requirement: Solver GPUStep records its RenderGraph internally

Each solver's `GPUStep(vk::CommandBuffer cb)` implementation SHALL call `m_rg->RecordAllPasses(cb)` after updating per-frame uniforms and ensuring the RG exists. The caller (PhysicsSystem) SHALL NOT access the solver's RG handle.

The solver SHALL access `RenderSystem&` through its internally stored reference (set at construction time) and `PhysicsScene&` through `m_bound_scene` (set via `OnBindToScene` during registration). Neither shall appear as method parameters.

#### Scenario: Solver records RG to shared CB

- **WHEN** `physics->GPUStep(cb)` is called between `cb.begin()` and `cb.end()`
- **THEN** each solver with a valid RG SHALL record its passes to `cb`
- **AND** the CB SHALL contain physics compute barriers + pass functions
