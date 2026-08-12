# Asset Type Placement Spec

## ADDED Requirements

### Requirement: Render-domain asset types live under engine/Render/
Mesh, Material, Shader, and Texture asset files (including their loaders `ObjLoader`, `GltfLoader`, `TextureImportUtils`, `MaterialUtils`, `ImportTypes`, `ImportSharedUtil`, `Importer`) SHALL be located under `engine/Render/`.

#### Scenario: Render asset paths resolve
- **WHEN** any file includes a render-domain asset header (e.g. `Render/Mesh/MeshAsset.h`, `Render/Material/MaterialTemplateAsset.h`, `Render/Shader/ShaderAsset.h`, `Render/Texture/Image2DTextureAsset.h`, `Render/Loader/ObjLoader.h`)
- **THEN** the include resolves and the project builds

#### Scenario: No stale Asset group includes
- **WHEN** the change is complete
- **THEN** no include directive referencing `Asset/Mesh/`, `Asset/Material/`, `Asset/Shader/`, `Asset/Texture/`, or `Asset/Loader/` exists in the repository

### Requirement: Scene and URDF asset types live under engine/Framework/
`SceneAsset`, `LevelAsset`, `UrdfLoader`, and `UrdfTypes` SHALL be located under `engine/Framework/`.

#### Scenario: Framework asset paths resolve
- **WHEN** any file includes `Framework/Scene/SceneAsset.h`, `Framework/Scene/LevelAsset.h`, `Framework/Loader/UrdfLoader.h`, or `Framework/Loader/UrdfTypes.h`
- **THEN** the include resolves and the project builds

### Requirement: Asset core locations unchanged
The asset core files (`Asset.h`, `AssetRef.h`, `AssetManager/`, `AssetDatabase/`, `AssetRuntime.h`, `InstantiatedFromAsset.h`, `asset_export.h`) SHALL remain under `engine/Asset/`, and their include paths SHALL stay `Asset/...`.

#### Scenario: Core paths still resolve
- **WHEN** any file includes `Asset/Asset.h` or `Asset/AssetRef.h`
- **THEN** the include resolves to the `EngineAssetCore` DLL headers without modification

### Requirement: Build targets follow the relocation
The `EngineLibAsset` OBJECT library SHALL be dissolved; render-domain asset sources SHALL be compiled into `EngineLibRender` and scene/URDF sources into `EngineLibFramework` (both still bundled in engine.dll). External dependencies SHALL follow: tinyobjloader/stb/ktx/fastgltf with Render, tinyxml2 with Framework.

#### Scenario: engine.dll links without EngineLibAsset
- **WHEN** the project is configured and built
- **THEN** the `Engine` target no longer references `$<TARGET_OBJECTS:EngineLibAsset>` and links successfully

#### Scenario: Asset loading behavior unchanged
- **WHEN** ctest runs
- **THEN** all tests pass, including those exercising asset load/save

### Requirement: Serialized assets remain compatible
The relocation SHALL NOT change reflection registration or serialization: `%type` strings keep `Engine::` namespaces and existing asset files load identically.

#### Scenario: Existing asset files load unchanged
- **WHEN** an asset file saved before the relocation is loaded after it
- **THEN** it deserializes with identical content and type name
