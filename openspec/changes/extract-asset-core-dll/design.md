# Design: Extract EngineAssetCore DLL

## Context

The engine currently builds one `engine.dll` bundling five OBJECT libraries (Asset, Framework, Physics, Render, UserInterface) plus `MainClass` and imgui. Only Core and Rhi have been extracted as DLLs, each with its own reflection task (`meta_core`, `meta_rhi`) and registration entry (`RegisterCoreTypes`, `RegisterRhiTypes`) invoked by `MainClass` at startup.

The Asset module is the dependency root of the remaining bundle: its 30 headers mix core infrastructure with concrete asset types. Concrete asset types include Render headers (`MeshAsset.h → Render/Renderer/VertexAttribute.h`) and Framework headers (`SceneAsset`/`LevelAsset` serialize GameObject/Component object graphs), producing Asset↔Render and Asset↔Framework cycles. A key fact found during investigation: **the asset core infrastructure itself depends on no concrete asset type** — `AssetManager.cpp`'s `ObjLoader.h` include is dead code, `AssetDatabase` is a pure interface with no `.cpp`, and `AssetRef`/`Asset`/`FileSystemDatabase` include only core-internal headers, AnnoRefl, and the `MainClass.h` service locator. This makes the core extractable as a DLL without any asset-type relocation.

## Goals / Non-Goals

**Goals:**
- Extract `EngineAssetCore.dll` containing Asset/AssetRef/AssetManager/AssetDatabase/FileSystemDatabase/InstantiatedFromAsset + new AssetRuntime registry.
- Remove all 8 `MainClass::GetInstance()` service-locator call sites inside the core via the AssetRuntime registry.
- Establish `meta_asset_core` reflection task and `RegisterAssetCoreTypes()` following the Core/Rhi pattern.
- Keep the DLL-extraction commit free of include-path changes (core files stay physically in `engine/Asset/`).
- Keep serialized asset files fully compatible (`%type` names unchanged).

**Non-Goals:**
- Relocating concrete asset types into Render/Framework/Physics directories (follow-up change; include paths and CMake dependency transfer there).
- DLL-extracting Render/Framework/Physics/UserInterface.
- Cleaning Render→Framework back edges or the `MainClass::GetInstance()` call sites outside AssetCore (e.g. `ShaderAsset.cpp`, `LevelAsset.cpp`, `UrdfLoader.cpp` — still inside engine.dll, no link cycle).
- World-assembly refactor (SystemContext injection).

## Decisions

### D1. AssetCore boundary: the 6 existing core headers + new AssetRuntime
Core DLL sources: `Asset.h/.cpp`, `AssetRef.h/.cpp`, `AssetManager/AssetManager.h/.cpp`, `AssetDatabase/AssetDatabase.h`, `AssetDatabase/FileSystemDatabase.h/.cpp`, `InstantiatedFromAsset.h` (template-only), new `AssetRuntime.h/.cpp`.
- `AssetDatabase.h` is pure interface (no `.cpp` exists).
- `InstantiatedFromAsset.h` is a header-only template interface; no export macro needed.
- Rationale: this is exactly the set with zero concrete-asset-type dependencies (verified by include audit).
- Alternative considered: converting the whole Asset OBJECT library into a DLL — rejected: asset types include Render/Framework headers, forming an immediate DLL cycle.

### D2. Service-locator removal: `AssetRuntimeContext` module registry
```cpp
struct AssetRuntimeContext {
    AssetManager *asset_manager{nullptr};
    AssetDatabase *asset_database{nullptr};
};
ASSET_CORE_API void SetAssetRuntime(const AssetRuntimeContext &ctx) noexcept;
ASSET_CORE_API const AssetRuntimeContext &GetAssetRuntime() noexcept;
```
- Pointers live in a file-local global inside AssetCore (`AssetRuntime.cpp`); `AssetRef`/`AssetManager` read them through `GetAssetRuntime()` within the same DLL; `MainClass` seeds once at construction; `MainClass` clears (`SetAssetRuntime({})`) during destruction.
- Rationale: `AssetRef` is a value type (copied, serialized, embedded in scene objects) — constructor injection is impractical at every use site; a process-global registry is the only "inject once, query anywhere" mechanism that keeps `AssetManager` a plain class and makes tests independent of MainClass (tests can seed their own context).
- Alternatives considered: (a) constructor injection — fails for `AssetRef`; (b) `AssetManager` singleton — equivalent but binds the global access point to the class and leaves `AssetDatabase` unhandled; (c) direct singleton of MainClass itself — the status quo being removed.
- Precedent: same pattern as AnnoRefl's `Type::s_index_type_map` global type registry.

### D3. Core files stay physically in `engine/Asset/` (zero include churn)
- The DLL target lists its sources explicitly; `EngineLibAsset` OBJECT narrows to the six asset-type subdirectories (Mesh/Material/Shader/Texture/Scene/Loader).
- All existing include paths (`Asset/Asset.h`, `Asset/AssetRef.h`, ...) remain valid — nothing else in the repo changes.
- Rationale: DLL extraction correctness is about link cycles, reflection registration, and the cross-DLL type table — file placement is irrelevant to it. Keeping this commit free of include edits makes it minimal, reviewable, and rollback-safe. A later rename/move (if desired) can ride along with the asset-relocation task.
- Alternative considered: moving core files to `engine/AssetCore/` for directory-name consistency with Core/Rhi — rejected for this change: ~92 mechanical include edits, no technical gain.

### D4. Ordering: DLL extraction first, asset relocation deferred (not a prerequisite)
- Investigation showed the core has zero asset-type dependencies *today*, so relocation is not required before DLL extraction.
- Asset relocation becomes an independent follow-up task (pure file moves + include updates + external-dependency transfer), unaffected by and not affecting this change's correctness.

### D5. Reflection: `meta_asset_core` without parent chain, `meta_engine` excludes 2 headers
- `Asset.h` and `AssetRef.h` are the only REFL types among the core headers (filter is content-based on `REFL_SER_CLASS`).
- `meta_asset_core` mirrors `meta_rhi` (no `parent_projects`): GUID has no reflection macros, so no Core type pkl is needed for parsing `AssetRef.h` (its `save_to_archive`/`load_from_archive` are hand-written, storing only the GUID value).
- `meta_engine`'s `REFLECTION_SEARCH_HEADERS` (or the GLOB filter list) excludes `Asset/Asset.h` and `Asset/AssetRef.h`; all concrete asset types keep being registered by `meta_engine` (they remain inside engine.dll).
- `MainClass.cpp` registration chain becomes: `RegisterCoreTypes()` → `RegisterRhiTypes()` → `RegisterAssetCoreTypes()` → `RegisterAllTypes()`. Both DLLs write into the same AnnoRefl global type table (static member defined in AnnoRefl.dll, class exported with ANROREFL_API), so `AssetManager::LoadAssetImmediately` inside AssetCore can resolve `Engine::MeshAsset` registered by engine.dll.

### D6. Build wiring
- `EngineAssetCore` SHARED links: `EngineLibHeaderInterface`, `EngineCore` (GUID), `AnnoRefl`. No SDL (dead include removed), no glm (not used).
- `ASSET_CORE_API` export macro following the Core (`CORE_DLL_EXPORTS` → `CORE_API`) pattern: `ASSET_CORE_EXPORTS` private define + dllexport/dllimport wrapper header.
- `Engine` links `EngineAssetCore` PUBLIC so Editor/examples/tests (which link `Engine`) transitively get the import library and DLL dependency.
- POST_BUILD `copy_if_different` adds `EngineAssetCore.dll` (mirrors the existing EngineCore copy step).
- `EngineLibAsset` switches from `GLOB_RECURSE ./*.cpp` to subdirectory GL0Bs or a GLOB with a filter excluding the core sources, so the core `.cpp` files are not compiled twice.

## Risks / Trade-offs

- [Cross-DLL reflection registration order] → `RegisterAssetCoreTypes()` must be called before any asset load; it sits first in the chain alongside the other registration calls in `MainClass`; any asset loaded via `AssetManager` happens after startup registration.
- [Cross-DLL RTTI (`AssetRef::as<T>` dynamic_cast, `typeid` comparisons)] → already exercised by EngineCore/EngineRhi/EngineEditor; known pitfall documented: compare `std::type_info` by value, never by address.
- [AnnoRefl global type table shared across DLLs] → verified: `Type::s_index_type_map` is defined in AnnoRefl.dll and the class is exported; Core/Rhi already rely on this.
- [meta_asset_core generated-code locations] → parser emits into `engine/__generated__/<target>/`; the core `.cpp` files' `#include "__generated__/Asset.h.inc"` resolution must be verified after first configure; adjustment (explicit include dir) is local to the new target.
- [Missing EngineAssetCore.dll at runtime for tests/examples] → POST_BUILD copy to the engine output directory (same mechanism as EngineCore).
- [Behavior change risk from service-locator swap] → `GetAssetRuntime()` returns the same pointers MainClass used to provide; test suite (ctest) covers asset load/store paths; each stage is an independently revertable commit.

## Migration Plan

1. **Stage 0 (independent commit):** dead include cleanup + `AssetRuntime` registry + replace 8 service-locator call sites; `MainClass` seeds/clears the registry. Verify: clean build + ctest 48/48.
2. **Stage 1 (independent commit):** new `EngineAssetCore` target + `ASSET_CORE_API` + `meta_asset_core` + `RegisterAssetCoreTypes` + `meta_engine` exclusion + Engine PUBLIC link + POST_BUILD copy + `EngineLibAsset` narrowing. Verify: clean build + ctest 48/48 + Editor/example smoke run.
3. **Rollback:** revert the offending commit; each stage is independent and does not rely on the other.
4. **Follow-up (separate change):** asset-type relocation into Render/Framework (+ URDF into Framework), include-path updates, external-dependency transfer (tinyobjloader/stb/ktx/fastgltf → Render, tinyxml2 → Framework).

## Open Questions

- Whether `meta_asset_core` needs a `parent_projects` entry after all (expected: no, since GUID has no REFL macros; verify at first configure; add `meta_core_generation` if the parser reports missing type info).
- Whether to clear the `AssetRuntimeContext` in `MainClass`'s destructor or rely on documented lifetime ("valid while MainClass lives"); default: clear in destructor for safety.
