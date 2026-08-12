# Tasks: Relocate asset types to their owning modules

## 1. Move render-domain asset files to engine/Render/

- [x] 1.1 `git mv engine/Asset/Mesh engine/Render/Mesh` (MeshAsset, PlaneMeshAsset)
- [x] 1.2 `git mv engine/Asset/Material engine/Render/Material` (MaterialAsset, MaterialTemplateAsset, MaterialLibraryAsset, PipelineProperty)
- [x] 1.3 `git mv engine/Asset/Shader engine/Render/Shader` (ShaderAsset, ShaderCompiler, ShaderIncluder)
- [x] 1.4 `git mv engine/Asset/Texture engine/Render/Texture` (TextureAsset, Image2DTextureAsset, ImageCubemapAsset, SolidColorTextureAsset)
- [x] 1.5 Move render loaders to `engine/Render/Loader/`: ObjLoader, GltfLoader, TextureImportUtils, MaterialUtils, MaterialUtilsGltf, ImportTypes, ImportSharedUtil, Importer (`git mv` + create dir)

## 2. Move scene/URDF asset files to engine/Framework/

- [x] 2.1 `git mv engine/Asset/Scene engine/Framework/Scene` (SceneAsset, LevelAsset)
- [x] 2.2 Move UrdfLoader/UrdfTypes to `engine/Framework/Loader/` (`git mv` + create dir)

## 3. Update include paths

- [x] 3.1 Replace `Asset/Mesh/` → `Render/Mesh/` across engine/example/editor/tests/test (11 sites)
- [x] 3.2 Replace `Asset/Material/` → `Render/Material/` (22 sites)
- [x] 3.3 Replace `Asset/Shader/` → `Render/Shader/` (13 sites)
- [x] 3.4 Replace `Asset/Texture/` → `Render/Texture/` (15 sites)
- [x] 3.5 Replace `Asset/Scene/` → `Framework/Scene/` (11 sites)
- [x] 3.6 Replace render-loader includes `Asset/Loader/{ObjLoader,GltfLoader,TextureImportUtils,MaterialUtils,ImportTypes,ImportSharedUtil,Importer}` → `Render/Loader/...`; Urdf includes → `Framework/Loader/...` (10 sites)
- [x] 3.7 Grep assert: no `Asset/Mesh|Material|Shader|Texture|Scene|Loader` include remains anywhere

## 4. CMake: dissolve EngineLibAsset

- [x] 4.1 `engine/Render/CMakeLists.txt`: GLOB the new Mesh/Material/Shader/Texture/Loader subdirectories into `EngineLibRender`; link `tinyobjloader`, `stb`, `ktx`, `fastgltf::fastgltf`
- [x] 4.2 `engine/Framework/CMakeLists.txt`: GLOB the new Scene/Loader subdirectories into `EngineLibFramework`; link `tinyxml2`
- [x] 4.3 `engine/Asset/CMakeLists.txt`: remove the `EngineLibAsset` OBJECT target and its dependency links
- [x] 4.4 `engine/CMakeLists.txt`: drop `$<TARGET_OBJECTS:EngineLibAsset>` from the `Engine` target
- [x] 4.5 Reconfigure (`cmake --preset debug`) and build clean

## 5. Verify

- [x] 5.1 `ctest --preset debug` all green (48/48)
- [x] 5.2 Final grep for stale `Asset/<type-group>/` includes in repository
- [x] 5.3 Confirm `meta_engine` registration unchanged (`%type` names) and asset-loading tests pass
