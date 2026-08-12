# Design: Relocate asset types to their owning modules

## Context

After `extract-asset-core-dll`, `engine/Asset/` contains: the core infrastructure (compiled into `EngineAssetCore.dll`) and 24 concrete asset-type files (compiled into `EngineLibAsset` OBJECT inside engine.dll). The concrete types carry include paths like `Asset/Mesh/MeshAsset.h`, `Asset/Material/...`, `Asset/Shader/...`, `Asset/Texture/...`, `Asset/Scene/...`, `Asset/Loader/...`, referenced by ~82 include sites across engine, example, editor, and tests.

The dependency reality inside engine.dll (verified during grilling):
- Render-domain assets (Mesh/Material/Shader/Texture + obj/gltf importers) reference `Render/` and `Rhi/` headers.
- Scene/Level assets serialize Framework object graphs and call WorldSystem/RenderSystem (via MainClass service locator — out of scope here, same-DLL).
- UrdfLoader imports robot descriptions into Framework scene/component data.
- `Importer.h` has no users (dead interface); it follows the Loader group to Render.

## Goals / Non-Goals

**Goals:**
- Move render-domain asset files under `engine/Render/`, scene/URDF asset files under `engine/Framework/`.
- Update every include path that referenced the old `Asset/<type>/...` locations.
- Dissolve `EngineLibAsset`; its sources join `EngineLibRender`/`EngineLibFramework` OBJECT targets (engine.dll contents unchanged in behavior).
- Keep serialized asset files and reflection registration identical.

**Non-Goals:**
- DLL-extracting Render/Framework (still bundled in engine.dll).
- Removing service-locator (`MainClass::GetInstance()`) call sites in moved files (e.g. `LevelAsset.cpp`, `UrdfLoader.cpp`, `ShaderAsset.cpp`) — they stay legal inside the single DLL; to be addressed at each module's DLL extraction.
- Changing `Asset/` core paths (`Asset/Asset.h` etc.) — those stay as the `EngineAssetCore` DLL's locations.
- Deleting the now-empty `engine/Asset/` type subdirectories' CMake references beyond the OBJECT split.

## Decisions

### D1. Target layout
- `engine/Render/Mesh/` (MeshAsset, PlaneMeshAsset), `engine/Render/Material/` (MaterialAsset, MaterialTemplateAsset, MaterialLibraryAsset, PipelineProperty), `engine/Render/Shader/` (ShaderAsset, ShaderCompiler, ShaderIncluder), `engine/Render/Texture/` (TextureAsset, Image2DTextureAsset, ImageCubemapAsset, SolidColorTextureAsset), `engine/Render/Loader/` (ObjLoader, GltfLoader, TextureImportUtils, MaterialUtils, MaterialUtilsGltf, ImportTypes, ImportSharedUtil, Importer).
- `engine/Framework/Scene/` (SceneAsset, LevelAsset), `engine/Framework/Loader/` (UrdfLoader, UrdfTypes).
- Rationale: co-locate each type with the module that owns its runtime semantics; flat per-module subdirectories mirror the existing `Render/Renderer/`/`Framework/world/` style.
- Alternative considered: `Render/Asset/Mesh/` nesting to keep "asset" naming visible — rejected: adds a redundant directory level and keeps the artificial "Asset" grouping alive.

### D2. Include paths
- `Asset/Mesh/X` → `Render/Mesh/X`; `Asset/Material/X` → `Render/Material/X`; `Asset/Shader/X` → `Render/Shader/X`; `Asset/Texture/X` → `Render/Texture/X`; `Asset/Scene/X` → `Framework/Scene/X`; `Asset/Loader/X` → `Render/Loader/X` or `Framework/Loader/X` per file.
- Mechanical prefix replacement per group; verified by a grep that no `Asset/Mesh|Material|Shader|Texture|Scene|Loader` include remains.
- `Asset/Asset.h`, `Asset/AssetRef.h`, `Asset/AssetManager/...`, `Asset/AssetDatabase/...`, `Asset/InstantiatedFromAsset.h`, `Asset/asset_export.h`, `Asset/AssetRuntime.h` are core paths and stay untouched.

### D3. CMake: dissolve EngineLibAsset
- `EngineLibRender` gains GLOB sources for its new subdirectories and links `tinyobjloader`, `stb`, `ktx`, `fastgltf::fastgltf`.
- `EngineLibFramework` gains GLOB sources for its new subdirectories and links `tinyxml2`.
- `EngineLibAsset` OBJECT target and its `$<TARGET_OBJECTS:EngineLibAsset>` entry in the `Engine` target are removed; the Asset CMakeLists retains only the `EngineAssetCore` DLL target and `meta_asset_core`.
- `meta_engine` keeps scanning `engine/` recursively — relocated headers are picked up automatically; reflection output and `%type` names unchanged.

### D4. Moved files keep their logic untouched
- No code changes beyond include-path updates (headers' self-includes within the same group use the same new prefix; cross-group includes like `MaterialTemplateAsset → Rhi/Texture/ImageUtils.h` are already module-relative and stay).
- `ShaderIncluder.cpp`'s `MainClass.h` include and `LevelAsset.cpp`/`UrdfLoader.cpp` service-locator calls remain (same-DLL, out of scope).

## Risks / Trade-offs

- [Missed include sites] → systematic prefix replacements per group + final grep asserting zero stale `Asset/<type-group>/` includes; build + ctest catch the rest.
- [Framework gains Render-asset dependency (ObjTestMeshComponent → Render/Loader/ObjLoader.h)] → legal inside engine.dll; consistent with Framework's top-level role in the future architecture.
- [GLOB staleness] → any newly moved source requires a CMake reconfigure (GLOB is fixed at configure time); the implementation runs configure before build.
- [Reflection regression from path change] → `meta_engine` input list changes but registered type names (`Engine::MeshAsset` etc.) do not; verify with the existing asset-loading tests in ctest.

## Migration Plan

1. Move render-domain files to `engine/Render/` (git mv preserves history).
2. Move scene/URDF files to `engine/Framework/` (git mv).
3. Apply prefix include replacements across engine/example/editor/tests/test.
4. Update CMake: EngineLibRender/EngineLibFramework sources + deps; remove EngineLibAsset; drop its OBJECT from Engine.
5. Reconfigure, build, ctest 48/48; grep for stale includes.
6. Rollback: single revert; the change is mechanical.

## Open Questions

- None expected; if the reflection parser reports issues due to path changes, the fix is confined to the CMake input lists.
