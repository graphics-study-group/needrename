# Proposal: Relocate asset types to their owning modules

## Why

`engine/Asset/` currently mixes the asset core infrastructure (now `EngineAssetCore.dll`, extracted in `extract-asset-core-dll`) with concrete asset types that semantically belong to other modules: Mesh/Material/Shader/Texture assets are render-domain data, Scene/Level assets are Framework object-graph data, URDF assets are Framework scene data after import. Keeping them under `Asset/` forces every module to reach into the asset directory for types it owns conceptually, and blocks the future DLL extraction of Render/Framework/Physics (a module's assets must live inside the module before the module can become a DLL). This change relocates the files and their include paths while keeping everything inside engine.dll — a pure structural move with zero behavior change.

## What Changes

- **Relocate to `Render/`**: `Mesh/`, `Material/`, `Shader/` (incl. ShaderCompiler/ShaderIncluder), `Texture/`, plus Loader files `ObjLoader`, `GltfLoader`, `TextureImportUtils`, `MaterialUtils`, `ImportTypes`, `ImportSharedUtil`, `Importer`.
- **Relocate to `Framework/`**: `Scene/` (`SceneAsset`, `LevelAsset`), plus Loader files `UrdfLoader`, `UrdfTypes` (URDF import produces Framework scene/component data).
- **Update ~82 include paths** across engine/example/editor/tests referencing `Asset/Mesh/`, `Asset/Material/`, `Asset/Shader/`, `Asset/Texture/`, `Asset/Scene/`, `Asset/Loader/` to their new module paths.
- **CMake**: dissolve the `EngineLibAsset` OBJECT library — its sources move into `EngineLibRender`/`EngineLibFramework` OBJECT targets (still bundled in engine.dll); external dependencies transfer with them (tinyobjloader/stb/ktx/fastgltf → Render, tinyxml2 → Framework).
- **No serialization change**: `%type` strings keep `Engine::` namespaces; asset files stay compatible.

## Capabilities

### New Capabilities
- `asset-type-placement`: Concrete asset types are co-located with the module that owns their runtime semantics — render assets under `engine/Render/`, scene/URDF assets under `engine/Framework/` — while the asset core remains the single shared data-layer DLL.

### Modified Capabilities
<!-- None: no existing spec's requirements change; `asset-core-module` requirements are unaffected (core files stay in place). -->

## Impact

- **Code**: ~82 include-path edits across `engine/` (Render/Framework/Physics/Asset/MainClass), `example/`, `editor/`, `tests/`, `test/`; file moves only, no logic changes.
- **Build**: `EngineLibAsset` target removed; `EngineLibRender`/`EngineLibFramework` gain asset-type sources and their external deps; `engine/CMakeLists.txt` drops the `EngineLibAsset` OBJECT from the `Engine` target.
- **Reflection**: `meta_engine` still scans the relocated headers (GLOB over `engine/`) — registration unchanged; `%type` names unchanged.
- **Dependencies inside engine.dll**: Framework components may now include `Render/...` asset headers (e.g. `ObjTestMeshComponent → Render/Loader/ObjLoader.h`) — legal inside the single DLL; service-locator call sites (e.g. `LevelAsset.cpp`, `UrdfLoader.cpp`) stay as-is, to be addressed when those modules become DLLs.
