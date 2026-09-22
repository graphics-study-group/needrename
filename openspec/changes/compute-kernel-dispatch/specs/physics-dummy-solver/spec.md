# physics-dummy-solver

## MODIFIED Requirements

### Requirement: DummySolver dispatches compute directly in GPUStep

On each `GPUStep(cb)` call, the solver SHALL:
1. Insert a `vk::MemoryBarrier2` (ComputeShader: ShaderStorageWrite → ComputeShader: ShaderStorageRead|Write) at the start
2. Dispatch the compute shader through the compute kernel dispatch surface
3. Use the kernel acquired during `PreGPUStep`

The compute shader SHALL displace each alive body by `position.z += gravity.z * time_step` and write its model matrix.

`DummySolver::PreGPUStep()` SHALL perform the shader initialization, the uniform buffer write, and the kernel acquisition. `DummySolver::GPUStep(cb)` SHALL only dispatch.

#### Scenario: Bodies move downward each frame

- **WHEN** `GPUStep(cb)` is called with 3 rigid bodies, `gravity = (0,0,-9.81)`, `time_step = 0.01`
- **THEN** the push-constant block SHALL contain `vec4(0, 0, -9.81, 0.01)` recorded before the dispatch
- **AND** compute dispatch SHALL be recorded directly to `cb` (no `RenderGraph::RecordAllPasses`)

#### Scenario: No RenderGraph used

- **WHEN** `GPUStep()` is called for any frame
- **THEN** no `RenderGraph`, `RenderGraphBuilder`, or `RenderGraphPass` is created or used
- **AND** the dispatch is recorded directly through the kernel dispatch surface

#### Scenario: Dispatch reuses the kernel acquired earlier

- **WHEN** `GPUStep(cb)` is called
- **THEN** the kernel it dispatches was already acquired during `PreGPUStep`
- **AND** no pipeline or shader module is created during `GPUStep`
