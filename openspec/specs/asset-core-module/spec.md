# Asset Core Module Spec

## ADDED Requirements

### Requirement: EngineAssetCore is a standalone shared library
The asset core infrastructure SHALL be built as a shared library target named `EngineAssetCore` (producing `EngineAssetCore.dll`), containing the `Asset` base class, `AssetRef`, `AssetManager`, `AssetDatabase` interface, `FileSystemDatabase`, and `InstantiatedFromAsset`.

#### Scenario: Core DLL links independently
- **WHEN** the project is configured and built
- **THEN** an `EngineAssetCore.dll` is produced without any engine-module (Render/Framework/Physics/UI) headers in its include graph

#### Scenario: Engine depends on the core DLL
- **WHEN** `engine.dll` is linked
- **THEN** it links `EngineAssetCore` PUBLIC and its runtime deployment includes `EngineAssetCore.dll` next to `EngineCore.dll` and `EngineRhi.dll`

### Requirement: AssetCore has no engine-module dependencies
No header or source file in the asset core SHALL include any header from Render, Framework, Physics, UserInterface, or MainClass.

#### Scenario: Compile-time independence
- **WHEN** the `EngineAssetCore` target is compiled
- **THEN** no include directive referencing `Render/`, `Framework/`, `Physics/`, `UserInterface/`, or `MainClass.h` is present in its translation units

#### Scenario: Dead includes removed
- **WHEN** the AssetCore extraction lands
- **THEN** the dead includes `Render/AttachmentUtils.h` (MaterialTemplateAsset), `MainClass.h`/`Render/RenderSystem.h`/`Rhi/Device/DeviceInterface.h` (ShaderCompiler), `Asset/Loader/ObjLoader.h` (AssetManager), and `SDL3/SDL.h` (AssetManager) are removed

### Requirement: AssetRuntime service registry
The asset core SHALL provide a module-level registry (`AssetRuntimeContext`) with `SetAssetRuntime`/`GetAssetRuntime` functions that store pointers to the active `AssetManager` and `AssetDatabase`.

#### Scenario: AssetRef acquires through the registry
- **WHEN** `AssetRef::Acquire`, `AssetRef::Release`, `AssetRef::as`, `AssetRef::TryGetAsset`, or `AssetRef::LoadEagerly` is invoked
- **THEN** the call resolves the `AssetManager` through `GetAssetRuntime()` instead of `MainClass::GetInstance()`

#### Scenario: AssetManager loads through the registry
- **WHEN** `AssetManager::LoadAssetImmediately` or the loading queue loads an archive
- **THEN** the call resolves the `AssetDatabase` through `GetAssetRuntime()` instead of `MainClass::GetInstance()`

#### Scenario: MainClass seeds the registry
- **WHEN** `MainClass` finishes constructing its subsystems
- **THEN** it calls `SetAssetRuntime({asset_manager.get(), asset_database.get()})` exactly once

#### Scenario: Tests can seed their own registry
- **WHEN** a test executable links only the asset core (without MainClass) and calls `SetAssetRuntime` with its own manager/database instances
- **THEN** `AssetRef` and `AssetManager` operate against those instances

### Requirement: Per-DLL reflection registration
The asset core SHALL be parsed by its own reflection task (`meta_asset_core`) exposing `RegisterAssetCoreTypes()`, and `meta_engine` SHALL stop registering `Asset` and `AssetRef`.

#### Scenario: Registration chain
- **WHEN** `MainClass` initializes
- **THEN** `RegisterAssetCoreTypes()` is invoked alongside `RegisterCoreTypes()`, `RegisterRhiTypes()`, and `RegisterAllTypes()`

#### Scenario: Cross-DLL type resolution
- **WHEN** `AssetManager` (inside `EngineAssetCore.dll`) loads an archive whose `%type` names a concrete asset registered by engine.dll (e.g. `Engine::MeshAsset`)
- **THEN** the type resolves through the shared AnnoRefl type table and the asset instance is created successfully

### Requirement: Serialized asset compatibility
The extraction SHALL NOT change serialized asset representation: `%type` strings and the JSON layout produced by asset save/load SHALL remain byte-identical to the pre-extraction format.

#### Scenario: Existing asset files load unchanged
- **WHEN** an asset file saved before the extraction is loaded after the extraction
- **THEN** it deserializes with identical content and type name (`Engine::...` namespaces unchanged)

### Requirement: Asset type headers remain at current locations
The concrete asset types (Mesh, Material, Shader, Texture, Scene, Loader) SHALL keep their physical locations under `engine/Asset/` and their include paths during this change; no relocation is performed.

#### Scenario: No include-path churn
- **WHEN** the extraction change is applied
- **THEN** no file outside the asset core sources, `MainClass`, or the CMake files requires an include-path edit
