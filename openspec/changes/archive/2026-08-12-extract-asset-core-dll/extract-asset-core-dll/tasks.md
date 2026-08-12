# Tasks: Extract EngineAssetCore DLL

## 1. Stage 0: Dead includes and service-locator removal

- [x] 1.1 Remove dead includes in Asset module: `MaterialTemplateAsset.h` → `Render/AttachmentUtils.h`; `ShaderCompiler.cpp` → `MainClass.h`/`Render/RenderSystem.h`/`Rhi/Device/DeviceInterface.h`; `AssetManager.cpp` → `Asset/Loader/ObjLoader.h`; `AssetManager.h` → `SDL3/SDL.h`
- [x] 1.2 Remove dead includes in Render module: `RenderSystem.cpp` → `Framework/component/RenderComponent/RendererComponent.h`; `CommandBuffer.cpp` → same; `RendererManager.h` → `Framework/world/Handle.h`
- [x] 1.3 Add `AssetRuntime.h`/`AssetRuntime.cpp` in `engine/Asset/` defining `AssetRuntimeContext` (`AssetManager*`, `AssetDatabase*`) with exported `SetAssetRuntime`/`GetAssetRuntime` backed by a file-local global
- [x] 1.4 Replace 5 `MainClass::GetInstance()->GetAssetManager()` call sites in `AssetRef.cpp` (Acquire, Release, TryGetAsset, LoadEagerly, as) with `GetAssetRuntime().asset_manager`
- [x] 1.5 Replace 2 `MainClass::GetInstance()->GetAssetDatabase()` call sites in `AssetManager.cpp` (load queue + `LoadAssetImmediately`) with `GetAssetRuntime().asset_database`
- [x] 1.6 Remove the unused `MainClass.h` include in `Asset.cpp` (no `GetInstance` usage in the file)
- [x] 1.7 Update `MainClass.cpp`: call `SetAssetRuntime({asset_manager.get(), asset_database.get()})` after subsystem construction; clear the context in the destructor
- [x] 1.8 Verify: clean build + `ctest --preset debug` all green

## 2. Stage 1: EngineAssetCore DLL extraction

- [x] 2.1 Add `ASSET_CORE_API` export macro (dllexport/dllimport wrapper keyed on `ASSET_CORE_EXPORTS`, following the Core `CORE_API` pattern) in a new export header under `engine/Asset/`
- [x] 2.2 Create `EngineAssetCore` SHARED target in `engine/Asset/CMakeLists.txt` with explicit sources (`Asset.cpp`, `AssetRef.cpp`, `AssetManager/AssetManager.cpp`, `AssetDatabase/FileSystemDatabase.cpp`, `AssetRuntime.cpp`) linking `EngineLibHeaderInterface`, `EngineCore`, `AnnoRefl`; define `ASSET_CORE_EXPORTS` privately
- [x] 2.3 Narrow `EngineLibAsset` OBJECT to the six asset-type subdirectories (Mesh/Material/Shader/Texture/Scene/Loader) so core `.cpp` files are not compiled twice
- [x] 2.4 Add `meta_asset_core` reflection task in `engine/Asset/CMakeLists.txt` parsing `Asset.h` and `AssetRef.h` (no parent projects), following the `meta_rhi` pattern
- [x] 2.5 Add `AssetReflectionRegistration.cpp` (mirroring `CoreReflectionRegistration.cpp`) exposing `RegisterAssetCoreTypes()`
- [x] 2.6 Exclude `Asset/Asset.h` and `Asset/AssetRef.h` from the `meta_engine` scan in `engine/CMakeLists.txt` (filter step), keeping all concrete asset types in `meta_engine`
- [x] 2.7 Update `MainClass.cpp` registration chain: `RegisterCoreTypes()` → `RegisterRhiTypes()` → `RegisterAssetCoreTypes()` → `RegisterAllTypes()`
- [x] 2.8 Update `engine/CMakeLists.txt`: add `EngineAssetCore` to `Engine` PUBLIC links; add `EngineAssetCore.dll` to the POST_BUILD copy list
- [x] 2.9 Verify generated-code include resolution: `Asset.cpp`/`AssetRef.cpp` find their `__generated__/*.inc` files from `meta_asset_core` (adjust include dirs on the new target if the parser output location differs)
- [x] 2.10 Verify: clean build + `ctest --preset debug` all green + Editor/example smoke run

## 3. Final verification

- [x] 3.1 Confirm zero engine-module includes inside the `EngineAssetCore` target (grep for `Render/`, `Framework/`, `Physics/`, `UserInterface/`, `MainClass.h` in the target sources)
- [x] 3.2 Confirm `%type` serialized strings unchanged (`Engine::` namespace untouched) and existing asset files load identically
- [x] 3.3 Confirm test/example/Editor binaries pick up `EngineAssetCore.dll` at runtime (deployment directory check)
