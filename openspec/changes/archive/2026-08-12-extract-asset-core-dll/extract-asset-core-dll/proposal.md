# Proposal: Extract EngineAssetCore DLL

## Why

Engine.dll still bundles five OBJECT libraries (Asset, Framework, Physics, Render, UserInterface). Asset is the dependency root of that bundle: it mixes core infrastructure (Asset base class, AssetRef, AssetManager, AssetDatabase) with concrete asset types (Mesh/Material/Shader/Texture/Scene/Urdf) that include Render and Framework headers, forming Asset↔Render and Asset↔Framework header cycles that block any of them from becoming a DLL. Extracting the asset core infrastructure as its own DLL is the first unlock: it removes the dependency root from the bundle, validates the per-DLL reflection pattern (already proven by Core/Rhi) for the remaining modules, and makes every later DLL extraction (Render, Framework, Physics) independent of the asset cycle.

## What Changes

- **New `EngineAssetCore.dll`** (SHARED) containing the core asset infrastructure: `Asset`, `AssetRef`, `AssetManager`, `AssetDatabase` (interface), `FileSystemDatabase`, `InstantiatedFromAsset`, plus a new `AssetRuntime` registration mechanism.
- **New `AssetRuntimeContext` registry**: module-level `SetAssetRuntime`/`GetAssetRuntime` replacing the 8 `MainClass::GetInstance()` service-locator call sites inside AssetCore (`AssetRef.cpp` ×5, `AssetManager.cpp` ×2, `Asset.cpp` ×1). MainClass seeds the registry once at construction.
- **Dead include cleanup**: 8 includes removed (Asset side: `MaterialTemplateAsset→AttachmentUtils`, `ShaderCompiler→MainClass/RenderSystem/DeviceInterface`, `AssetManager.cpp→ObjLoader`, `AssetManager.h→SDL3`; Render side: `RenderSystem.cpp`/`CommandBuffer.cpp→RendererComponent`, `RendererManager.h→Handle.h`).
- **New `meta_asset_core` reflection task** generating `RegisterAssetCoreTypes()`; `meta_engine` stops scanning `Asset.h`/`AssetRef.h` (the only REFL types moving to the core DLL).
- **Engine links `EngineAssetCore` PUBLIC** (propagated to Editor/examples/tests); POST_BUILD copies `EngineAssetCore.dll` next to the other DLLs.
- **Asset type headers keep their physical location and include paths unchanged** in this change; relocation of concrete asset types into Render/Framework/Physics is a separate follow-up change.
- **No serialization format change**: `%type` strings keep the `Engine::` namespace, existing asset files remain compatible.

## Capabilities

### New Capabilities
- `asset-core-module`: EngineAssetCore.dll as the root data-layer DLL — Asset base class, ref-counted AssetRef, AssetManager lifecycle, AssetDatabase storage abstraction, AssetRuntime service registry, and per-DLL reflection registration.

### Modified Capabilities
<!-- None: no existing spec's requirements change; this is a new module extraction following the core-module/rhi-module pattern. -->

## Impact

- **Build**: new CMake target in `engine/Asset/CMakeLists.txt`; `EngineLibAsset` OBJECT narrows to asset-type subdirectories; `engine/CMakeLists.txt` adds the DLL to `Engine` PUBLIC links and the POST_BUILD copy list.
- **Reflection**: `meta_asset_core` parses the two core headers; `meta_engine` excludes them; `MainClass.cpp` calls `RegisterAssetCoreTypes()` alongside `RegisterCoreTypes`/`RegisterRhiTypes`/`RegisterAllTypes`.
- **Dependency direction**: EngineAssetCore depends only on EngineCore (GUID), AnnoRefl, and the header interface — no engine module headers; engine.dll (with all asset types and systems) depends on EngineAssetCore.
- **Runtime**: `AssetRuntime` replaces service-locator access inside AssetCore; MainClass seeds it after subsystem construction.
- **Unaffected**: asset files on disk, serialized type names, test binaries (all link `Engine` transitively), Editor/examples (link `Engine`).
