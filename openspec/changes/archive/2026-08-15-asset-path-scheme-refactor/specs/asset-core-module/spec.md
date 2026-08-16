# asset-core-module

## ADDED Requirements

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
