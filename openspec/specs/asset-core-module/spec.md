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

### Requirement: AssetDatabase exposes asset path lookup
The `AssetDatabase` interface SHALL declare `virtual AssetPath GetAssetPath(GUID guid) const = 0` so that consumers (notably `ShaderAsset`) can resolve an asset's in-project path through the abstraction. `FileSystemDatabase` SHALL override this method (its existing implementation remains).

#### Scenario: Interface declares the pure virtual
- **WHEN** `engine/Asset/AssetDatabase/AssetDatabase.h` is inspected
- **THEN** it declares `virtual AssetPath GetAssetPath(GUID guid) const = 0`

#### Scenario: FileSystemDatabase overrides it
- **WHEN** `engine/Asset/AssetDatabase/FileSystemDatabase.h` is inspected
- **THEN** `GetAssetPath` is declared `override`

#### Scenario: ShaderAsset resolves its source path through the interface
- **WHEN** `ShaderAsset::Compile()` needs the shader's on-disk source path
- **THEN** it SHALL call `GetAssetRuntime().asset_database->GetAssetPath(GetGUID())` to obtain the in-project `AssetPath`, then downcast the database to `FileSystemDatabase` and resolve it to disk via `ToAbsolutePath` (see asset-path "Disk-path resolution is a FileSystemDatabase concern")

### Requirement: AssetDatabase exposes path-based lookup
The `AssetDatabase` interface SHALL expose the path secondary index alongside the GUID primary key: `virtual AssetRef GetNewAssetRef(const AssetPath &path) = 0` SHALL return an unloaded `AssetRef` for the asset at the given path and throw `std::runtime_error` when the path is unknown, and `virtual std::optional<GUID> GetGUID(const AssetPath &path) const = 0` SHALL return the asset's GUID or `std::nullopt` when the path is unknown. `FileSystemDatabase` SHALL implement both against its path-to-GUID map.

#### Scenario: Interface declares the pure virtuals
- **WHEN** `engine/Asset/AssetDatabase/AssetDatabase.h` is inspected
- **THEN** it declares `GetNewAssetRef(const AssetPath&)` and `GetGUID(const AssetPath&)` as pure virtuals and does not forward-declare `FileSystemDatabase`

#### Scenario: FileSystemDatabase resolves path lookups
- **WHEN** `GetNewAssetRef` is called with a path registered in the database
- **THEN** an `AssetRef` carrying the matching GUID is returned without loading the asset

#### Scenario: Unknown path lookup
- **WHEN** `GetGUID` is called with an unregistered path
- **THEN** it returns `std::nullopt` instead of throwing

#### Scenario: Interface has no implementation dependency
- **WHEN** `AssetDatabase.h` is compiled
- **THEN** no `FileSystemDatabase` type is named anywhere in the header (the `GetAssetPath` return type `AssetPath` no longer references it)

### Requirement: FileSystemDatabase mounts asset schemes
`FileSystemDatabase` SHALL provide `RegisterScheme(std::string_view scheme, const std::filesystem::path &root)` to add a scheme mount. `LoadBuiltinAssets(const std::filesystem::path &path)` SHALL register the `builtin` scheme to `path` and then scan it for `.asset` files, and `LoadProjectAssets(const std::filesystem::path &path)` SHALL register the `res` scheme to `path` and scan it; both keep their existing signatures and call-site contracts.

#### Scenario: Builtin load registers and scans
- **WHEN** `LoadBuiltinAssets(ENGINE_BUILTIN_ASSETS_DIR)` is called on a fresh `FileSystemDatabase`
- **THEN** every `.asset` file under that directory is registered under a `builtin://` path relative to the mount root

#### Scenario: Project load registers and scans
- **WHEN** `LoadProjectAssets(project_dir)` is called
- **THEN** every `.asset` file under `project_dir` is registered under a `res://` path relative to the mount root

#### Scenario: Custom scheme registration
- **WHEN** `RegisterScheme("usr", some_dir)` is called
- **THEN** paths with the `usr` scheme resolve against `some_dir` and the existing `res`/`builtin` mounts are unaffected

### Requirement: GUID remains primary asset identity
The path index SHALL NOT replace the GUID as primary identity: `AssetRef` SHALL continue to serialize only its GUID, and path-changing operations (`MovePath`) SHALL preserve each asset's GUID while remapping its path entry.

#### Scenario: AssetRef serialization unchanged
- **WHEN** an `AssetRef` is saved to an archive
- **THEN** only the GUID is written, with no path string in the serialized form

#### Scenario: Move preserves GUID
- **WHEN** `MovePath` moves an asset file to a new `res://` path
- **THEN** the asset keeps its GUID and `GetAssetPath(GUID)` returns the new path
