## Why

Physics is GPU physics (XPBD compute dispatches), yet every GPU facility it needs — allocator, compute pipelines, submission channel, buffers — is obtained through `RenderSystem`. This inverts the conceptual dependency: physics (a compute workload) depends on render (a display consumer). Meanwhile, the Render module's `DeviceBuffer` / `ComputeBuffer` / `Texture` / `ComputeStage` / `SubmissionHelper` / `ImmutableResourceCache` are generic GPU infrastructure with no rendering semantics; only the render algorithm layer (`FrameManager`, `RendererManager`, `SceneDataManager`, `RenderGraph`, materials) is truly rendering-specific. Extracting the generic layer into an independent `Rhi` module lets Physics and Render depend on it equally, paves the way for headless physics (no window, no rendering), and incidentally fixes the wrong `Asset → Render` header dependency (becomes `Asset → Rhi`).

## What Changes

- **BREAKING** Rename module `GpuContext` → `Rhi` (directory `engine/Rhi/`, DLL `Rhi.dll`, CMake target `Rhi`). Namespace `Engine::RenderSystemState` / `Engine::ImageUtils` / `Engine::PipelineUtils` / bare `Engine` (for moved types) unify into `Engine::Rhi`.
- **BREAKING** Delete the `GpuContext` aggregator class (dead code; a future headless-init entry point can be designed when needed).
- Move generic GPU infrastructure from `engine/Render/` into `engine/Rhi/`: `DeviceBuffer`, `ComputeBuffer`, `StructuredBuffer`, `StructuredBufferPlacer`, `Texture`, `ImageTexture`, `TextureSubresourceView`, `ImageUtils`, `ImmutableResourceCache`, `SubmissionHelper`, `ComputeStage`, `ComputeResourceBinding`, `ShaderResourceBinding`, `ShaderParameterLayout` (incl. `ShdrRfl::SPLayout`), `ShaderInterface`, `MemoryAccessTypes`, `PipelineEnums`.
- **BREAKING** `ComputeStage` drops its Asset coupling: remove `IInstantiatedFromAsset<ShaderAsset>` inheritance and the `Instantiate(ShaderAsset&)` overload; callers pass SPIR-V binary + name directly (the only current Asset-path user is the bloom stage in `ComplexRenderGraphBuilder`).
- **BREAKING** Physics interface rework: constructors change from `RenderSystem&` to `(const Rhi::DeviceInterface&, const Rhi::AllocatorState&)` (per-component, as needed); `ISolver::GPUStep` and detector/algorithm `Record` take raw `vk::CommandBuffer` instead of `Engine::CommandBuffer&`.
- **BREAKING** The physics→render model matrices bridge moves from the solver (`XPBDGpuSolver` calling `SceneDataManager::SetModelMatricesBuffer`) to the `MainClass` assembly layer (physics exposes `GetGpuBuffers().model_matrices`, `MainClass::RunOneFrame` forwards it).
- `Rhi` links SPIRV-Cross (moved from Render); `Rhi` introduces reflection registration for the first time (per-DLL `RegisterAllTypes`, generated `__generated__` moved to the Rhi module).
- JSON asset files get namespace-qualified type names updated by a batch script.

## Capabilities

### New Capabilities
- `rhi-module`: The `Rhi` shared library as the engine's generic GPU infrastructure layer — device, allocator, buffers, textures, immutable resource cache, upload/submission queue, compute pipeline facilities — with the unified `Engine::Rhi` namespace, independent of Render.

### Modified Capabilities
- `gpu-context-module`: Module renamed to `Rhi`; the `GpuContext` aggregator class is removed; scope extends from device+allocator to the full moved infrastructure set.
- `submission-helper-sync`: `SubmissionHelper` relocates from `Render/RenderSystem/` to the `Rhi` module with namespace `Engine::Rhi`; state machine and parameterized-signal behavior unchanged.
- `physics-solver-interface`: Solver/detector/algorithm constructors take `Rhi` references instead of `RenderSystem&`; `GPUStep` records into raw `vk::CommandBuffer`.
- `render-graph-model-matrix-input`: `SetModelMatricesBuffer` is no longer called by the physics solver; the model matrices buffer is forwarded by the `MainClass` assembly layer.
- `physics-main-loop-integration`: `MainClass::RunOneFrame` performs the physics→render model matrices bridge between `GPUStep` and render-graph recording.

## Impact

- **Code**: `engine/Render/*` (large file removal), `engine/GpuContext/*` (rename to `Rhi`), `engine/Physics/*` (constructor signatures, GPUStep), `engine/Asset/Material/PipelineProperty.h` (namespace refs), `engine/MainClass.cpp` (bridge), `engine/Render/Pipeline/Compute/ComputeStage.*`, tests under `engine/Tests/` and `test/`, examples.
- **Build**: CMake (new `Rhi` target, Render loses files, SPIRV-Cross dependency moves to `Rhi`), reflection parser scan list gains `engine/Rhi/`, `gpu_context_export.h` → `rhi_export.h` (`RHI_API`).
- **Assets**: JSON asset files with namespace-qualified type names updated via batch script.
- **API**: physics solver/detector/algorithm constructor signatures, `ISolver::GPUStep`, relocated classes and unified namespace.
