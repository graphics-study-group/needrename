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

### D2: DeviceContext aggregator (revives the deleted GpuContext class, redesigned)

The `GpuContext` aggregator class was deleted in Phase 2 as dead code. Phase 3 analysis revived the need: `AllocatorState` and `ImmutableResourceCache` are device-scoped facilities, and physics now requires the same set as rendering. A new aggregator `Rhi::DeviceContext` is introduced (named `DeviceContext` — the module is `Rhi`, the device-scoped context is `DeviceContext`; user decision):

```cpp
namespace Engine::Rhi {
    class DeviceContext {
        std::unique_ptr<DeviceInterface> m_device_interface;
        std::unique_ptr<AllocatorState> m_allocator_state;
        std::unique_ptr<ImmutableResourceCache> m_immutable_resource_cache;
    public:
        explicit DeviceContext(DeviceInterface::DeviceConfiguration cfg);  // creates device → dispatcher init → irc → allocator
        DeviceInterface& GetDeviceInterface(); const DeviceInterface& GetDeviceInterface() const;
        AllocatorState& GetAllocatorState(); const AllocatorState& GetAllocatorState() const;
        ImmutableResourceCache& GetIRCache();
        vk::Device GetDevice() const;
    };
}
```

- **Contains**: DeviceInterface + AllocatorState + ImmutableResourceCache (static device-scoped facilities, one lifetime = one device).
- **Does NOT contain**: `SubmissionHelper` (active object with pending-queue state; physics and render keep independent upload queues — a shared queue would let physics `ExecuteSubmissionImmediately` carry along render-layer pending operations), `PresentProvider`/`FrameManager` (render presentation layer).
- **Ownership**: `MainClass` owns one `std::unique_ptr<Rhi::DeviceContext>`, created in `Initialize`; passes it to `RenderSystem` (constructor) and to physics components.
- **`VULKAN_HPP_DEFAULT_DISPATCHER.init()` moves into `DeviceContext` construction** (whoever creates the device initializes the loader); removed from `RenderSystem::Create`. Standalone tests keep their own init (they construct `DeviceInterface` directly, not via `DeviceContext`).
- **`AllocatorState` constructor simplifies to one step**: `AllocatorState(DeviceInterface&)` (replaces the `AllocatorState()` + `SetDeviceInterface()` + `Create()` two-step; user decision). Tests adapt syntactically but keep constructing the facilities directly (no `DeviceContext` use; user decision).
- **RenderSystem rework**: constructor gains `Rhi::DeviceContext&`; `Create()` drops its own device/irc/allocator creation and uses the context; `GetDeviceInterface()/GetAllocatorState()/GetIRCache()` remain as forwards; a `GetDeviceContext()` accessor is added.
- **PhysicsAdaptor (Framework)** lazily creates its own `Rhi::SubmissionHelper` on first `Flush` (it receives `RenderSystem&` per call, no construction-time device), and passes `(render_system.GetDeviceContext(), m_submission_helper)` to `PhysicsScene::SyncGpuBuffers`.

Rationale for the name: the previous deletion was correct at the time (no consumer); the aggregator is now a real composition with a single owner and two consumers, and `DeviceContext` states its device-scoped nature precisely.

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

Remove `IInstantiatedFromAsset<ShaderAsset>` inheritance and `Instantiate(ShaderAsset&)`; keep `Instantiate(const std::vector<uint32_t>& code, std::string_view name)` (implementation at `ComputeStage.cpp:98`). The only Asset-path caller (`ComplexRenderGraphBuilder.cpp:84`, bloom) switches to passing `shader->binary` + name. `ComputeStage` construction changes from `RenderSystem&` to `Rhi::DeviceContext&` — it needs the IRC because `AllocateResourceBinding()` creates a `ComputeResourceBinding`, whose `ShaderResourceBinding` requires an IRC. (Unified consumer pattern, see D5.)

Alternative considered: keep the Asset overload and link Asset into Rhi — rejected (would drag reflection into the lowest layer, reversing `external_dependency_granular_split`).

### D5: Physics interface rework

- `ISolver::GPUStep(CommandBuffer&)` → `GPUStep(vk::CommandBuffer)`. Implementations (`XpbdGpuSolver`, `DummySolver`) and `Record` methods of `SpatialHashBroadDetector`, `ConvexCollisionDetector`, `RadixSort`, `ParallelScan`, `CompactUnique` adapt: raw `pipelineBarrier2` calls already use the raw handle; the wrapper calls (`BindComputeStage`/`BindComputeResource`/`DispatchCompute`) become free functions in `Engine::Rhi` operating on `vk::CommandBuffer` + `ComputeStage`/`ComputeResourceBinding`:
  ```cpp
  namespace Engine::Rhi {
      void BindComputeStage(vk::CommandBuffer cb, ComputeStage& stage);
      void BindComputeResource(vk::CommandBuffer cb, ComputeResourceBinding& binding, uint32_t frame_index);
      void DispatchCompute(vk::CommandBuffer cb, uint32_t x, uint32_t y, uint32_t z);
  }
  ```
  `BindComputeResource` needs the in-flight frame index (descriptor-set rotation) that `CommandBuffer` used to own (`m_inflight_frame_index`); physics components maintain their own frame counter (incremented per `GPUStep`/`Record`, modulo the binding's frame count) — the descriptor rotation semantics are preserved without the render fif.
- Constructors: `XpbdGpuSolver(RenderSystem&)` → `(Rhi::DeviceContext&)`; same for `DummySolver`, detectors, algorithms (`RadixSort`/`ParallelScan`/`CompactUnique` keep their `uint32_t max_elem_count` second parameter). Components take the `DeviceContext&` (D2) and read `GetDeviceInterface()`/`GetAllocatorState()`/`GetIRCache()` from it — physics shares the render-side IRC (single cache per device).
- **All device-facility consumers unify on a single `DeviceContext&`** (user decision after grilling): `ComputeStage(DeviceContext&)`, `ComputeResourceBinding(DeviceContext&, ComputeStage&)`, `Texture(DeviceContext&, ...)` / `ImageTexture` / `RenderTargetTexture` (Texture's stale `RenderSystem&` constructor was a Phase 3 gap, now closed). `AllocatorState(DeviceInterface&)` stays (it is an aggregatee inside `DeviceContext`, not a consumer — taking `DeviceContext&` would be circular). `ShaderResourceBinding(ImmutableResourceCache&)` stays minimal (single-facility dependency).
- `RenderSystem` keeps its `GetDeviceInterface()`/`GetAllocatorState()`/`GetIRCache()`/`GetDevice()` as one-line forwards to `m_device_context` (Facade pattern — 50+ in-module call sites stay unchanged), plus `GetDeviceContext()`.
- `ComputeResourceBinding(RenderSystem&, ComputeStage&)` → `(const Rhi::DeviceInterface&, const Rhi::AllocatorState&, Rhi::ImmutableResourceCache&, ComputeStage&)` (it needs the allocator for `IndexedBuffer` creation, `QueryLimit` from the device interface, and the IRC for its `ShaderResourceBinding`).
- `PhysicsScene::RefreshGpuBuffers`/`SyncGpuBuffers` take `(Rhi::DeviceContext&, Rhi::SubmissionHelper&)`; `PhysicsAdaptor::Flush` lazily creates and holds the scene's own `SubmissionHelper`.
- A single `DeviceContext&` parameter replaces the earlier `(DeviceInterface&, AllocatorState&)` plan: the aggregator (D2) makes the signature shorter and gives physics the shared IRC without per-component construction.

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

- CMake Phase 1 (implemented): `engine/Rhi/CMakeLists.txt` builds `EngineLibRhi` as an OBJECT library merged into `engine.dll` (user decision — a standalone `Rhi.dll` split is deferred to Phase 4, which also restores `GPU_CONTEXT_DLL_EXPORTS`/`VULKAN_HPP_STORAGE_SHARED_EXPORT` semantics and the dispatcher storage). Render loses the moved sources and the `spirv-cross-cpp` PUBLIC link; `EngineLibRhi` links `spirv-cross-cpp` + `vma`.
- `GPU_CONTEXT_API` expands empty during the OBJECT merge: with a `dllimport` class attribute, MinGW treats class-member definitions as import references and strips them from the export table, which in turn disables the linker's default `--export-all-symbols` and drops every non-`dllexport` engine symbol (MainClass etc.) from `libengine.dll.a`. With the macro empty, `--export-all-symbols` stays active (baseline behavior, 165k symbols) and auto-import serves consumers.
- The dispatcher storage `VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE` in `DeviceInterface.cpp` is removed while merged (MainClass.cpp provides it); restored in Phase 4.
- Reflection: moved types carry `REFL_SER_CLASS` (e.g. `ImageUtils::ImageFormat`, `PipelineUtils::*`). The parser globs `engine/` recursively, so wrappers regenerate under `engine/Rhi/__generated__/` automatically (verified: `ImageUtils.h.inc`, `PipelineEnums.h.inc`); `EngineLibRhi` depends on `meta_engine`. Per-DLL `RegisterRhiTypes()` registration remains Phase 4 work.
- JSON assets: script rewrites namespace-qualified type names in Phase 2.

### D8: Phase 1 dependency-closure additions (implemented)

Files discovered during relocation that must move with the closure (all generic GPU facilities, no rendering semantics):
- `Hasher.hpp` (FNV hasher, used by `ImmutableResourceCache` and `ShaderResourceBinding`)
- `ImageUtilsFunc.h` (ToVk* conversion functions, used by `ImmutableResourceCache` / `SubmissionHelper`)
- `PipelineInfo.h` (contains `ComputePassInfo`; `MaterialPoolInfo` is a descriptor-pool size constant, no render dependency)
- `ToVkCompareOp` moved from `PipelineUtils.hpp` into `PipelineEnums.h` (only cross-file user of the enum-to-vk converters that depends on Rhi; `PipelineUtils.hpp` stays in Render and includes the Rhi header)
- `IndexedBuffer` is deferred to Phase 3: `ComputeResourceBinding` uses it, but while merged into `engine.dll` the link order is fine; it moves together with the `ComputeResourceBinding` constructor rework.

## Risks / Trade-offs

- [Large mechanical surface (whole-repo namespace + include updates)] → Phase 1 is pure file moves (build-verified, zero behavior change); Phase 2 namespace unification is script-assisted; Phase 3 is the only behavior-change phase.
- [JSON asset type names break after namespace unification] → Batch script rewrite + manual spot checks; accepted per user decision.
- [Reflection registration first introduced into Rhi DLL] → Follow the proven per-DLL pattern; `RegisterRhiTypes` called from `MainClass::Initialize` before asset loading.
- [Physics behavior regression during interface rework] → Phase 3 keeps semantics identical (same queues, same barriers, same dispatch order); full test regression (windowed + headless + engine/Tests).
- [`ExecuteSubmissionImmediately` interaction with render-layer Enqueues] → Already resolved by the per-batch staging accounting (D4 of the decouple change); behavior unchanged by relocation.
- [Future multi-queue CommandBuffer may want physics back] → Documented non-goal; free-function compute helpers are compatible with pass-level recording later.

## Migration Plan

Four sequential phases, each ending with a build + test gate and a manual review + commit by the user:

1. **Phase 1 — Pure relocation (DONE)**: move files Render → Rhi, update include paths and CMake only. No namespace changes, no logic changes. `EngineLibRhi` is an OBJECT library merged into `engine.dll` (user decision; the standalone DLL split moves to Phase 4). Build + ctest green (48/48), behavior identical.
2. **Phase 2 — Namespace unification**: rename everything to `Engine::Rhi`, update all references repo-wide, JSON asset batch script. Build + ctest green.
3. **Phase 3 — Physics interface rework + DeviceContext**: create `Rhi::DeviceContext` aggregator, `AllocatorState` one-step constructor, MainClass/RenderSystem init rework, physics constructor signatures to `DeviceContext&`, raw `vk::CommandBuffer` in GPUStep/Record, free-function compute helpers (with frame index), `ComputeStage`/`ComputeResourceBinding` constructors (+IRC), bloom Asset-overload removal, `IndexedBuffer` move, model matrices bridge to MainClass. Build + ctest green.
4. **Phase 4 — Cleanup + DLL split**: reflection registration for Rhi, `rhi_export.h`, restore SHARED `Rhi.dll` + `GPU_CONTEXT_DLL_EXPORTS` + dispatcher storage, test/example updates. Build + ctest green.

Rollback: no data migration beyond JSON asset renames (reversible via script); each phase is an independent revertable diff.

## Open Questions

**RESOLVED** during grilling:
- ComputeStage Asset coupling → dropped (D4).
- CommandBuffer split → not split; physics uses raw `vk::CommandBuffer` (non-goal).
- SubmissionHelper batch sync → already self-owned state machine (decouple change).
- Texture family moves → yes, with `ImageUtils` + `ImmutableResourceCache` (D3).
- PipelineEnums moves wholesale → yes (D3).
- Namespace strategy → unified `Engine::Rhi`, assets scripted (D1).
- `GpuContext` class → deleted in Phase 2; revived as `Rhi::DeviceContext` aggregator in Phase 3 (D2).
- Physics component signatures → single `Rhi::DeviceContext&` via the aggregator (D5), superseding the earlier `(DeviceInterface&, AllocatorState&)` plan.
- Model matrices bridge → `MainClass` assembly (D6).
- Change organization → single change, phased tasks, per-phase review + manual commit.
