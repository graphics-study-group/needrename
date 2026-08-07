## Context

The engine splits into modules where `Render`, `Physics`, and `Framework` are OBJECT libraries merged into the final executable, while `GpuContext` (device + allocator) is an independent shared DLL. Physics is GPU physics: the XPBD solver, broad/narrow collision detectors, and GPU algorithms (RadixSort/ParallelScan/CompactUnique) all obtain their GPU facilities through `RenderSystem &` — `GetAllocatorState()`, `GetDevice()` (via `ComputeStage` construction), and `GetFrameManager().GetSubmissionHelper()`. A prior change (`decouple-submission-helper-from-framemanager`, archived) already removed all `FrameManager` coupling from `SubmissionHelper`: it now constructs from `(const DeviceInterface&, const AllocatorState&)`, takes a caller-provided `vk::SemaphoreSignalInfo`, and enforces a Reset/Submitted state machine with per-batch staging accounting.

`GpuContext` currently holds `DeviceInterface` + `AllocatorState` only; the `GpuContext` aggregator class is unused dead code (RenderSystem constructs the components directly). The Render module contains a large set of generic GPU facilities that have no rendering semantics: `DeviceBuffer`, `ComputeBuffer`, `StructuredBuffer`, `Texture`, `ImageUtils`, `ImmutableResourceCache` (Fossilize-style hashing cache), `SubmissionHelper`, `ComputeStage`, `ShaderResourceBinding` family, `MemoryAccessTypes`, `PipelineEnums`. `ComputeStage`'s only Asset coupling is a 3-line forwarding overload used by a single caller (bloom in `ComplexRenderGraphBuilder`); physics uses the SPIR-V binary + name overload exclusively.

## Goals / Non-Goals

**Goals:**
- Establish `Rhi` as the engine's generic GPU infrastructure layer: module (directory/DLL/CMake target) and unified namespace `Engine::Rhi`, fully independent of Render.
- Move all generic GPU facilities from `engine/Render/` into `engine/Rhi/` with zero semantic changes.
- Decouple Physics from Render: constructors take Rhi references; `GPUStep`/`Record` take raw `vk::CommandBuffer`; the model matrices bridge moves to the `MainClass` assembly layer.
- Fix the `Asset → Render` header dependency to `Asset → Rhi`.
- Keep behavior identical at every phase; each phase independently buildable and testable.

**Non-Goals:**
- No rename of Render-side types that stay in Render (FrameManager/RendererManager/SceneDataManager/CameraManager/PresentProvider keep `Engine::RenderSystemState`; a future standalone naming change handles that).
- No move of `FrameSemaphore` (Render-internal timeline helper; physics does not use it).
- No `CommandBuffer` split: physics uses raw `vk::CommandBuffer`; the affinity-aware multi-queue `CommandBuffer` (future RenderGraph work) will re-evaluate physics migration later.
- No headless physics-only runtime loop (path B); physics still runs on facilities initialized by `RenderSystem` — only type-level coupling is removed.
- No `VULKAN_HPP_DEFAULT_DISPATCHER` relocation (still initialized in `RenderSystem::Create`; future headless-init entry will handle it).
- No performance work: `ExecuteSubmissionImmediately` keeps its current CPU-blocking semantics.

## Decisions

### D1: Module and namespace naming — `Rhi`, `Engine::Rhi`

The module is renamed from `GpuContext` to `Rhi` (directory `engine/Rhi/`, `Rhi.dll`, CMake target `Rhi`). All moved types unify into the single namespace `Engine::Rhi`:

| Current namespace | Types | Becomes |
|---|---|---|
| `Engine::RenderSystemState` | DeviceInterface, AllocatorState, SubmissionHelper, ImmutableResourceCache | `Engine::Rhi` |
| `Engine::ImageUtils` | ImageFormat, TextureDesc, SamplerDesc, helpers | `Engine::Rhi` |
| `Engine::PipelineUtils` | FillingMode, CullingMode, FrontFace, DSComparator, StencilOperation, ColorChannelMask, BlendOperation, BlendFactor | `Engine::Rhi` |
| `Engine::ShdrRfl` | SPLayout, SPInterface family | `Engine::Rhi` |
| bare `Engine` | DeviceBuffer, ComputeBuffer, StructuredBuffer, Texture, ImageTexture, ComputeStage, ComputeResourceBinding, ShaderResourceBinding, MemoryAccessTypes | `Engine::Rhi` |

Rationale: "Context" implies a stateful aggregator, but the module is a dependency-injected tool collection with no central object. `Rhi` (Render Hardware Interface, industry-standard term) is understood as the GPU abstraction layer serving both graphics and compute. Renaming now (before the migration) avoids a second full-repo namespace pass later. JSON assets with namespace-qualified type names are updated by a batch script (accepted cost).

Alternatives: keep `GpuContext` (semantics void after class deletion); `Gpu` (too terse); `GpuInfra` (verbose).

### D2: Delete the `GpuContext` aggregator class

`GpuContext::impl` (DeviceInterface + AllocatorState) is unused dead code. Delete the class; a future headless-init entry point will be designed when physics-only runtime actually needs one. Device/allocator composition stays explicit at call sites (RenderSystem already does this).

### D3: File migration set (Render → Rhi), zero semantic change

Moved as a group (dependency closure):

- `Memory/DeviceBuffer.h/.cpp`, `Memory/ComputeBuffer.h/.cpp`, `Memory/StructuredBuffer.h/.cpp`, `Memory/StructuredBufferPlacer.h/.cpp`
- `Memory/Texture.h/.cpp`, `Memory/ImageTexture.h/.cpp`, `Memory/TextureSubresourceView.h`
- `ImageUtils.h/.cpp`
- `Memory/MemoryAccessTypes.h`
- `RenderSystem/ImmutableResourceCache.h/.cpp`
- `RenderSystem/SubmissionHelper.h/.cpp` (already decoupled)
- `Pipeline/Compute/ComputeStage.h/.cpp`, `Pipeline/Compute/ComputeResourceBinding.h/.cpp`
- `Memory/ShaderParameters/ShaderResourceBinding.h/.cpp`, `ShaderParameterLayout.h/.cpp`, `ShaderInterface.h`
- `Pipeline/PipelineEnums.h`

Stays in Render: `FrameManager`, `RendererManager`, `SceneDataManager`, `CameraManager`, `ResizableRTTManager`, `PresentProvider`s, `CommandBuffer`, `Material*`, `RenderGraph*`, `Resource/*`, `Renderer/*`, `RenderTargetTexture`, `AttachmentUtils`, `FrameSemaphore`.

Notes:
- `RenderTargetTexture` (Render) inherits `Texture` (Rhi) — dependency direction Render → Rhi is correct.
- `SPLayout::Reflect` uses SPIRV-Cross (`third_party/SPIRV-Cross/spirv_cross.hpp`); the `spirv-cross-cpp` link moves from Render to `Rhi`.
- `ShaderInterface.h` depends only on `Core/flagbits.h` — no new dependency.
- `PipelineEnums.h` moves wholesale (not split): all enums are Vulkan pipeline-state mappings; splitting out only `DSComparator` would leave a severed set. `Asset/Material/PipelineProperty.h` (`using` aliases) and `SceneDataManager.cpp` update their references; this also fixes `Asset → Render` into `Asset → Rhi`.

### D4: ComputeStage drops Asset coupling

Remove `IInstantiatedFromAsset<ShaderAsset>` inheritance and `Instantiate(ShaderAsset&)`; keep `Instantiate(const std::vector<uint32_t>& code, std::string_view name)` (implementation at `ComputeStage.cpp:98`). The only Asset-path caller (`ComplexRenderGraphBuilder.cpp:83`, bloom) switches to passing `shader->binary` + name. `ComputeStage` construction changes from `RenderSystem&` to `(const DeviceInterface&, const AllocatorState&)` (it only needs `GetDevice()`; pipeline cache is `nullptr`).

Alternative considered: keep the Asset overload and link Asset into Rhi — rejected (would drag reflection into the lowest layer, reversing `external_dependency_granular_split`).

### D5: Physics interface rework

- `ISolver::GPUStep(CommandBuffer&)` → `GPUStep(vk::CommandBuffer)`. Implementations (`XpbdGpuSolver`, `DummySolver`) and `Record` methods of `SpatialHashBroadDetector`, `ConvexCollisionDetector`, `RadixSort`, `ParallelScan`, `CompactUnique` adapt: raw `pipelineBarrier2` calls already use the raw handle; the wrapper calls (`BindComputeStage`/`BindComputeResource`/`DispatchCompute`) become free functions in `Engine::Rhi` operating on `vk::CommandBuffer` + `ComputeStage`/`ComputeResourceBinding`.
- Constructors: `XpbdGpuSolver(RenderSystem&)` → `(const Rhi::DeviceInterface&, const Rhi::AllocatorState&)`; same for `DummySolver`, detectors, algorithms. `ComputeResourceBinding` → `(const Rhi::DeviceInterface&, ComputeStage&)`.
- `PhysicsScene::RefreshGpuBuffers`/`SyncGpuBuffers` take `(const Rhi::AllocatorState&, Rhi::SubmissionHelper&)`.
- Explicit per-component parameters over a new aggregate: honest, matches the dependency-injection style of the decoupled `SubmissionHelper`; `MainClass` already exposes the three getters (`GetDeviceInterface`, `GetAllocatorState`, `GetSubmissionHelper`).

### D6: Model matrices bridge moves to MainClass

Remove both physics-side `SetModelMatricesBuffer` calls: `XPBDGpuSolver.cpp:394` (per-frame in `GPUStep`) and `PhysicsScene::SyncGpuBuffers` (initialization-time, `PhysicsScene.cpp:247`). `MainClass::RunOneFrame` forwards the buffer once per frame (after `FlushPhysics`, which is when `SyncGpuBuffers` runs) and after `GPUStep`:

```cpp
renderer->GetSceneDataManager().SetModelMatricesBuffer(
    world->GetMainSceneRef().GetPhysicsScene()->GetGpuBuffers().model_matrices
);
```

`GetGpuBuffers()` already exposes `model_matrices` (ComputeBuffer*, upcast to `DeviceBuffer*` compatible). The buffer address changes only when `RefreshGpuBuffers` rebuilds; per-frame pointer sync is negligible. Physics no longer knows Render exists; the bridge is the coordinator's responsibility.

Alternatives: Render pulls from Physics (Render → Physics dependency, rejected); physics publishes via an interface (`IModelMatrixSink`, more machinery for equal benefit, rejected).

### D7: Rhi module build & reflection

- CMake: `engine/Rhi/CMakeLists.txt` becomes the target `Rhi` (SHARED) containing `GpuContext` sources + moved sources; links `Core`, `EngineDepSdl`, `EngineDepVulkan`, `EngineLibHeaderInterface`, `spirv-cross-cpp`, `vma`. Render loses the moved sources and the `spirv-cross-cpp` PUBLIC link.
- `gpu_context_export.h` → `rhi_export.h` with `RHI_API`; export macro applied to moved DLL-boundary types.
- Reflection: moved types carry `REFL_SER_CLASS` (e.g. `ImageUtils::ImageFormat`, `PipelineUtils::*`). Rhi gets its own generated `__generated__/` (e.g. `ImageUtils.h.inc`) and a per-DLL `RegisterRhiTypes()` following the existing per-DLL registration pattern (`RegisterCoreTypes`); `MainClass::Initialize` calls it. The reflection parser scan list gains `engine/Rhi/`.
- JSON assets: script rewrites namespace-qualified type names (e.g. `Engine::ImageUtils::ImageFormat` → `Engine::Rhi::ImageFormat`).

## Risks / Trade-offs

- [Large mechanical surface (whole-repo namespace + include updates)] → Phase 1 is pure file moves (build-verified, zero behavior change); Phase 2 namespace unification is script-assisted; Phase 3 is the only behavior-change phase.
- [JSON asset type names break after namespace unification] → Batch script rewrite + manual spot checks; accepted per user decision.
- [Reflection registration first introduced into Rhi DLL] → Follow the proven per-DLL pattern; `RegisterRhiTypes` called from `MainClass::Initialize` before asset loading.
- [Physics behavior regression during interface rework] → Phase 3 keeps semantics identical (same queues, same barriers, same dispatch order); full test regression (windowed + headless + engine/Tests).
- [`ExecuteSubmissionImmediately` interaction with render-layer Enqueues] → Already resolved by the per-batch staging accounting (D4 of the decouple change); behavior unchanged by relocation.
- [Future multi-queue CommandBuffer may want physics back] → Documented non-goal; free-function compute helpers are compatible with pass-level recording later.

## Migration Plan

Four sequential phases, each ending with a build + test gate and a manual review + commit by the user:

1. **Phase 1 — Pure relocation**: move files Render → Rhi, update include paths and CMake only. No namespace changes, no logic changes. Build + ctest green with identical behavior.
2. **Phase 2 — Namespace unification**: rename everything to `Engine::Rhi`, update all references repo-wide, JSON asset batch script. Build + ctest green.
3. **Phase 3 — Physics interface rework**: constructor signatures, raw `vk::CommandBuffer` in GPUStep/Record, free-function compute helpers, model matrices bridge to MainClass. Build + ctest green.
4. **Phase 4 — Cleanup**: reflection registration for Rhi, `rhi_export.h`, test/example updates, SPIRV-Cross dependency transfer. Build + ctest green.

Rollback: no data migration beyond JSON asset renames (reversible via script); each phase is an independent revertable diff.

## Open Questions

**RESOLVED** during grilling:
- ComputeStage Asset coupling → dropped (D4).
- CommandBuffer split → not split; physics uses raw `vk::CommandBuffer` (non-goal).
- SubmissionHelper batch sync → already self-owned state machine (decouple change).
- Texture family moves → yes, with `ImageUtils` + `ImmutableResourceCache` (D3).
- PipelineEnums moves wholesale → yes (D3).
- Namespace strategy → unified `Engine::Rhi`, assets scripted (D1).
- `GpuContext` class → deleted (D2).
- Physics component signatures → explicit `(DeviceInterface&, AllocatorState&)` (D5).
- Model matrices bridge → `MainClass` assembly (D6).
- Change organization → single change, phased tasks, per-phase review + manual commit.
