# Design: Framework Top Module and Render DLL Split

## Context

The engine has extracted four standalone DLLs so far: `EngineCore`, `EngineRhi` (with the GPU-compute infrastructure merged in — the former GpuContext work landed as `Rhi/Submission`, `Rhi/Texture`, `Rhi/Pipeline` under the `Engine::Rhi` namespace), `EngineAssetCore`, and `EnginePhysics`. Each extraction established the reusable pattern: per-DLL export macro header, per-DLL `meta_*` reflection target + `Register*Types()` function, PRIVATE inter-module CMake dependencies, and `EngineLibHeaderInterface` (the whole `engine/` root as include path) PUBLIC on every module so headers stay globally visible.

The remaining `Engine` SHARED library still absorbs three OBJECT libraries (`EngineLibFramework`, `EngineLibRender`, `EngineLibUserInterface`) plus `MainClass.cpp` and the imgui OBJECT objects. Two structural problems block the final split:

1. **Circular dependency**: Framework includes Render headers in 14 files (`WorldSystem.cpp`, `PhysicsAdaptor.cpp`, `UrdfLoader.cpp`, `LevelAsset.cpp`, `RendererComponent.h`, `CameraComponent.cpp`); Render includes Framework headers in 12 files (`GltfLoader.cpp`, `ObjLoader.cpp`, `Importer.cpp`, `ComplexRenderGraphBuilder.cpp`).
2. **Global service locator**: `MainClass::GetInstance()` is called from 21 sites — 12 inside Render (shader compilation, render-resource handle destruction, loaders), 9 inside Framework, 1 in Input. Lower layers reach upwards through a singleton, which is exactly what a DLL boundary forbids.

## Goals / Non-Goals

**Goals:**

- `EngineFramework.dll` as the single top-level module: owns `MainClass`, depends on every other module, orchestrates freely. `MainClass::GetInstance()` remains available but only to Framework internals.
- `EngineRender.dll` with zero `MainClass`/`Framework` includes, one-way dependencies (`Render → {AssetCore, Rhi, Core}`), owning `GUISystem` (as `Render/UserInterface/`).
- External-asset import layer (Gltf/Obj/Importer/Urdf + utilities) fully owned by Framework (`Framework/Import/`).
- Per-DLL reflection for both new DLLs; registration order Core → Rhi → AssetCore → Physics → Render → Framework.
- Tests and examples keep linking `Engine` (now an INTERFACE aggregation) with zero source changes beyond the `MainClass.h` include path.

**Non-Goals:**

- No behavioral redesign of the main loop, asset formats, render graph, or GPU resource lifetimes. Serialized output stays byte-identical.
- No removal of the `MainClass` singleton pattern itself (constructor injection of subsystems is deferred until a scripting/editor layer exists).
- No change to `EngineCore`/`EngineRhi`/`EngineAssetCore`/`EnginePhysics` internals (except the one `AssetDatabase::GetAssetPath` interface addition).
- No further splitting of Render into multiple DLLs (asset layer vs pipeline layer).

## Decisions

### D1. Framework becomes the top-level module; MainClass sits at `Framework/` root

Framework already hosts every cross-system bridge (`WorldSystem::UpdateRendererData(*renderer)`, `FlushPhysics(*renderer)`, `PhysicsAdaptor`). Making it the top module turns those bridges from "middleware with a back-edge" into "orchestrator with legitimate down-dependencies". MainClass goes to the `Framework/` root (no `App/` subdirectory — user decision), include path `<Framework/MainClass.h>`.

Alternatives considered: a separate `App`/`Shell` module — rejected: MainClass is pure orchestration over the same systems Framework already coordinates; a second orchestration layer adds a DLL and a naming problem without any boundary gain. Keeping `Engine.dll` as an aggregation shell — rejected: it would be a forwarder with no unique content.

### D2. Loader/Importer moves wholesale to `Framework/Import/`

The user's ownership principle: an asset *format* (how a `MeshAsset` serializes, how a `ShaderAsset` compiles) belongs to the module that owns the asset class; external *format import* (glTF/OBJ/URDF → engine assets + scene objects) is application-flavored and belongs to the top module. Therefore the entire `Render/Loader/` directory moves to `Framework/Import/` — including the pure utilities (`ImportSharedUtil`, `ImportTypes`, `MaterialUtils`, `TextureImportUtils`), because they only serve the import flow and Framework's "depend on everything" position has no side effects. Asset classes keep their own format logic inside Render (`MeshAsset` self-serialization, `ShaderAsset::LoadFromFile`).

Supporting facts verified in code: fastgltf/tinyobjloader/stb/ktx are referenced only from `Render/Loader/`; `MeshAsset`'s `friend class ObjLoader` is vestigial — every field ObjLoader touches (`m_name`, `m_submeshes`, MaterialAsset's `m_name`/`m_library`/`m_properties`, component asset refs) is public, so the friend declaration is deleted and asset construction stays `AssetManager::CreateAsset<T>()` + public-field fill. `GltfLoader`/`ObjLoader`/`UrdfLoader` constructors switch from `MainClass::GetInstance()` to `GetAssetRuntime()`; their `weak_ptr` members become raw pointers (`AssetManager*`/`FileSystemDatabase*`, downcast via `dynamic_cast`) because `AssetRuntimeContext` stores raw pointers, and their `lock()`/`expired()` uses become direct dereference with a null guard.

Alternative considered: keep raw-data parsing in Render and only move scene assembly — rejected as over-engineering; the whole loader layer is import plumbing, and splitting it would leave orphaned utilities in Render.

### D3. `RenderRuntimeContext` registry replaces MainClass lookups in Render

Patterned exactly on `AssetRuntimeContext` (`engine/Asset/AssetRuntime.h`), Render gains a module-level registry:

```cpp
// Render/RenderRuntime.h
struct RenderRuntimeContext {
    RenderSystem *render_system{nullptr};
    ShaderCompiler *shader_compiler{nullptr};   // editor-only; nullptr in shipped games
};
void SetRenderRuntime(const RenderRuntimeContext &ctx) noexcept;
const RenderRuntimeContext &GetRenderRuntime() noexcept;
```

`MainClass::Initialize` seeds it once after constructing `RenderSystem` (and `ShaderCompiler` in editor mode); `MainClass::~MainClass` clears it (mirroring the existing `SetAssetRuntime({})` teardown).

Consumers:
- `RenderResourceHandle::~RenderResourceHandle()` resolves `GetRenderRuntime().render_system`, then the typed manager, then `Release()` — replacing the `MainClass::GetInstance()` chain. The existing null-checks (`if (!rs) return;`) are preserved, so late teardown silently skips release exactly as today.
- `ShaderAsset::Compile()` resolves `GetShaderCompileService()` → shader compiler; returns false with a log when the service is absent (shipped games never reach GLSL compilation — SPIRV assets skip `Compile()` entirely).
- `DirStackFileIncluder`'s constructor stops querying MainClass: `ShaderCompiler`'s constructor accepts `const std::filesystem::path &project_assets_path` and forwards it as a system include path; MainClass (editor mode) passes `FileSystemDatabase::GetProjectAssetsPath()`.

Alternative considered (handle constructor injection of the manager pointer): rejected — it breaks the handle's lightweight POD-ness (index+generation), complicates copy semantics (a non-owning copy would carry a manager pointer), and cannot be initialized for deserialized handles; the module registry keeps the handle struct untouched, matching the `AssetRef` precedent.

### D4. `AssetDatabase::GetAssetPath` interface addition

`ShaderAsset` currently resolves its source path by downcasting `MainClass::GetInstance()->GetAssetDatabase()` to `FileSystemDatabase` and calling `GetAssetPath(GetGUID())`. The abstraction escape is fixed at the interface: `AssetDatabase` (AssetCore) gains `virtual AssetPath GetAssetPath(GUID guid) const = 0`; `FileSystemDatabase` overrides it (implementation already exists). `ShaderAsset::Compile` then calls `GetAssetRuntime().asset_database->GetAssetPath(GetGUID())`.

Alternative considered (storing the source path in the shader archive): rejected — hardcoding filesystem paths into assets breaks project relocation and import workflows; GUID-based resolution through the database interface is the durable contract.

### D5. UI module dissolves; GUISystem → `Render/UserInterface/`, Input → `Framework/Input/`

`GUISystem` is a render overlay (its `DrawGUI` records into `CommandBuffer`, `CreateVulkanBackend` takes `RenderSystem&`), so it belongs to Render; the imgui OBJECT objects (`$<TARGET_OBJECTS:imgui>`) compile into `EngineRender` and `EngineDepImgui` becomes Render-PRIVATE. `Input` feeds the world/component layer and merges into Framework. `Input::Update()` gains `float delta_time` (called from `MainClass::RunOneFrame` with `time->GetDeltaTime()`), removing Input's `MainClass` dependency. `Input` keeps its reflection annotation and joins the `meta_framework` scan.

Alternative considered (dedicated `EngineUserInterface` DLL): rejected — two files with hard couplings to Render (GUISystem) and Framework (Input) do not justify a DLL boundary.

### D6. Per-DLL reflection: `meta_render` + `meta_framework`

`meta_engine` is deleted. Each module follows the established recipe: GLOB headers, `filter_files_with_reflection_macros`, `add_reflection_parser` into the module's `__generated__/`, a `*ReflectionRegistration.cpp` exposing `extern "C" ..._API void Register*Types()`. `meta_framework` scans Framework headers **including `MainClass.h` and `Input.h`**. `MainClass.cpp` includes both `reflection_init.inc` files and calls, in order: `RegisterCoreTypes()`, `RegisterRhiTypes()`, `RegisterAssetCoreTypes()`, `RegisterPhysicsTypes()`, `RegisterRenderTypes()`, `RegisterFrameworkTypes()`, then `RegisterAllTypes()` (the engine-wide one registers the remaining `MainClass`-adjacent types under Framework's scan).

Cross-DLL type resolution already works via the shared AnnoRefl type table (proven by AssetCore loading `Engine::MeshAsset` from `%type` strings); no Framework serialized field directly names a Render type (verified: `LevelAsset`/`StaticMeshComponent` fields are `AssetRef`/handles only).

### D7. Dependency matrix — PRIVATE inter-module links

Following the Physics/AssetCore precedent: every module PUBLIC-links `EngineLibHeaderInterface` (global header visibility), modules PRIVATE-link each other, and each DLL explicitly lists every module whose symbols it directly uses. The one non-obvious entry: `EngineFramework` must PRIVATE-link `EngineRhi` because `PhysicsAdaptor.cpp` calls Rhi symbols directly, and Physics does not propagate its PRIVATE Rhi link.

| Target | PUBLIC | PRIVATE |
|---|---|---|
| `EngineRender` | `EngineLibHeaderInterface`, `AnnoRefl`, `EngineDepGlm` | `EngineAssetCore`, `EngineRhi`, `EngineCore`, `EngineDepVulkan`, `EngineDepSdl`, `EngineDepImgui`, `glslang`, `ktx`, `stb`, `meta_render`, `shader` |
| `EngineFramework` | `EngineLibHeaderInterface`, `AnnoRefl`, `EngineDepGlm` | `EngineRender`, `EnginePhysics`, `EngineAssetCore`, `EngineRhi`, `EngineCore`, `EngineDepVulkan`, `EngineDepSdl`, `EngineDepJson`, `tinyxml2`, `fastgltf`, `tinyobjloader`, `stb`, `ktx`, `meta_framework` |
| `Engine` (INTERFACE) | `EngineFramework`, `EngineRender`, `EnginePhysics`, `EngineAssetCore`, `EngineRhi`, `EngineCore` | — |

Export macros: `framework_export.h` / `render_export.h` keyed on `FRAMEWORK_DLL_EXPORTS` / `RENDER_DLL_EXPORTS`; **all** public classes annotated in one mechanical pass (user decision: full annotation, no minimal set).

### D8. Directory layout

```
Framework/                       (PascalCase unified during phase 1)
├── MainClass.h / .cpp           ← moved here, at the root (no App/)
├── World/      (world/: Scene, WorldSystem, Handle, EventQueue)
├── Object/     (object/: GameObject)
├── Component/  (component/: Transform/RenderComponent/physics)
├── Scene/      (SceneAsset, LevelAsset)
├── Import/     (GltfLoader, ObjLoader, Importer, ImportSharedUtil, ImportTypes,
│                MaterialUtils, TextureImportUtils, UrdfLoader, UrdfTypes)
├── Bridge/     (world/physics/: PhysicsAdaptor, PhysicsDescriptors, Internal/)
├── Input/      (Input.h/.cpp)
└── Tools/      (ComplexRenderGraphBuilder)

Render/
├── RenderSystem.h / FullRenderSystem.h / AttachmentUtils.h / render_export.h
├── Asset/       (Mesh/, Texture/, Material/, Shader/ asset classes + ShaderCompiler/Includer)
├── Resource/    (Resource/ managers + RenderResourceHandle + Memory/)
├── Pipeline/    (Pipeline/: RenderGraph, CommandBuffer, MaterialInstance + Renderer/)
├── RenderSystem/ (FrameManager, SceneDataManager, CameraManager, RendererManager, PresentProviders)
└── UserInterface/ (GUISystem)
```

The Render regrouping (Asset/Resource/Pipeline/RenderSystem) happens as its own step **after** both DLLs exist, using `git mv` only (zero logic changes); the Framework regrouping rides along with the phase-1 file moves.

### D9. Execution phases (each ends green and is reviewed + committed by the user)

1. **File moves only** (Engine still builds): 1a Loader → `Framework/Import/` + ComplexRenderGraphBuilder → `Framework/Tools/` + friend deletion + AssetRuntime switch; 1b GUISystem → `Render/UserInterface/`, Input → `Framework/Input/` (+ `Update(dt)`); 1c MainClass → `Framework/` + all include-path updates.
2. **Cut Render's upward deps**: `RenderRuntimeContext` + `ShaderCompiler` ctor param + `AssetDatabase::GetAssetPath` + dead include removal. Exit criterion: no `MainClass.h`/`Framework/` include remains under `Render/`.
3. **Reflection split**: `meta_render`, `meta_framework`, registration cpp files, MainClass registration chain, delete `meta_engine`.
4. **Render DLL**: `EngineRender` SHARED, `RENDER_API` everywhere, CMake rewire, POST_BUILD copy; `Engine` still builds from MainClass + Framework OBJECT.
5. **Framework DLL**: `EngineFramework` SHARED, `FRAMEWORK_API` everywhere, `Engine` target becomes INTERFACE aggregation, POST_BUILD final list; `Engine.dll` stops being produced.
6. **Cleanup**: Render internal regroup (`git mv`), delete `UserInterface/`, doxygen list, remove `EngineLibExternalDependency`, PCH check, `shader` dependency transfer.

## Risks / Trade-offs

- **[Mistaken PRIVATE links cause link errors on Windows]** → Each phase ends with a full build + ctest; the D7 matrix enumerates every cross-module symbol use (notably Framework → Rhi via PhysicsAdaptor).
- **[Shipped-game shader compilation path]** → SPIRV assets never call `Compile()`; when the service registry is empty, `ShaderAsset::Compile` logs and returns false instead of crashing (today it would dereference a null `shader_compiler` in non-editor builds — a latent bug this fixes).
- **[Teardown order]** → Handle destruction after `RenderSystem` destruction silently skips release (registry cleared). Verified: `MainClass` member order destroys `world` (handle owners) before `renderer`; even if reversed later, skipping release at process exit is harmless (GPU resources die with the device) and matches current `GetInstance()`-expired behavior.
- **[Reflection scan scope mistakes]** → `meta_framework` must include `MainClass.h`/`Input.h` or serialization silently misses types; verification step greps generated inc files against the header list.
- **[Friend-declaration removal hiding a private access]** → Verified field-by-field against ObjLoader.cpp usage; the build enforces it immediately.
- **[DLL boundary vs. template/static-member sharing]** → The AnnoRefl type table is already shared across DLLs (AssetCore ↔ engine.dll case); no new cross-DLL static state is introduced by this change.
- **[Engine INTERFACE target semantics]** → CMake INTERFACE targets export no binaries; any script that expected `Engine.dll` on disk must switch to `EngineFramework.dll` — examples/tests only link, so they are unaffected.

## Open Questions

None outstanding — all decision points were resolved during the grilling session (Engine.dll removal, meta split + registration order, Loader placement + friend removal, UI dissolution + `Update(dt)`, MainClass at `Framework/` root, full export-macro pass, six-phase plan with per-phase user review/commit, post-DLL Render regrouping, PRIVATE dependency matrix + `Engine` INTERFACE aggregation).
