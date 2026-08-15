# asset-core-module

## ADDED Requirements

### Requirement: AssetDatabase exposes asset path lookup
The `AssetDatabase` interface SHALL declare `virtual AssetPath GetAssetPath(GUID guid) const = 0` so that consumers (notably `ShaderAsset`) can resolve an asset's in-project path through the abstraction without downcasting to `FileSystemDatabase`. `FileSystemDatabase` SHALL override this method (its existing implementation remains).

#### Scenario: Interface declares the pure virtual
- **WHEN** `engine/Asset/AssetDatabase/AssetDatabase.h` is inspected
- **THEN** it declares `virtual AssetPath GetAssetPath(GUID guid) const = 0`

#### Scenario: FileSystemDatabase overrides it
- **WHEN** `engine/Asset/AssetDatabase/FileSystemDatabase.h` is inspected
- **THEN** `GetAssetPath` is declared `override`

#### Scenario: ShaderAsset resolves paths through the interface
- **WHEN** `ShaderAsset::Compile()` needs the shader's on-disk source path
- **THEN** it SHALL call `GetAssetRuntime().asset_database->GetAssetPath(GetGUID())` without casting the database to `FileSystemDatabase`
