# Physics Render Graph Handle Forwarding

## MODIFIED Requirements

### Requirement: Handle forwarding applies within a single RenderGraph

The handle forwarding pattern defined in this spec (solver imports scene buffers once, passes handles to detectors) SHALL apply within a single physics `RenderGraph`. When a rendering `RenderGraph` shares buffers with the physics `RenderGraph`, it SHALL import those buffers independently using `ImportExternalResource` with `prev_access` reflecting the state left by the physics `RenderGraph` (see `physics-render-graph-separation` spec).

This requirement is unchanged in its core behavior — the handle forwarding within the physics RenderGraph remains identical. The clarification adds the cross-RenderGraph boundary definition.

#### Scenario: Within-physics handle forwarding unchanged

- **WHEN** `XPBDGpuSolver::AddStepPasses()` imports scene buffers and passes handles to detectors
- **THEN** the behavior SHALL be identical to before this change — same handles used across solver and detector passes within the physics RenderGraph

## ADDED Requirements

### Requirement: Cross-RenderGraph buffer sharing uses prev_access

When a buffer is shared between the physics RenderGraph and the rendering RenderGraph, the rendering RenderGraph SHALL import the buffer independently (with its own `ImportExternalResource` call) and SHALL set `prev_access` to the access type the physics RenderGraph leaves the buffer with. This SHALL be `ShaderRandomWrite` for output buffers like `model_matrices`.

The physics RenderGraph SHALL NOT expose its internal `RGBufferHandle` values to the rendering RenderGraph. The two graphs SHALL be fully independent at the handle level, synchronized only through the shared `vk::CommandBuffer` recording order and correct `prev_access` declarations.

#### Scenario: Independent imports for shared buffer

- **WHEN** `model_matrices` is written by the physics RG and read by the rendering RG
- **THEN** both RGs SHALL call `ImportExternalResource` on the same `ComputeBuffer` instance
- **AND** the two imports SHALL return different `RGBufferHandle` values (one per builder)
- **AND** the rendering RG's import SHALL use `prev_access = ShaderRandomWrite`
- **AND** the physics RG's import SHALL use `prev_access = None`
