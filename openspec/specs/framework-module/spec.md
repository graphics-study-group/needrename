# framework-module Specification

## Purpose

Define the Framework module as the top-level standalone shared library `EngineFramework.dll`: it owns `MainClass` (at the `Framework/` root), the world/object/component core, `Input`, the external-asset import layer, the physics bridge, and the temporary render-graph builder. It depends on every other engine module (PRIVATE links), carries its own export macro and per-DLL reflection registration, and its internal directories use PascalCase.

## Requirements

### Requirement: Framework builds as EngineFramework shared library

The Framework module SHALL build as a shared library target named `EngineFramework` producing `EngineFramework.dll`, compiled from all sources under `engine/Framework/`. The target SHALL define `FRAMEWORK_DLL_EXPORTS` privately.

The target SHALL declare the following dependencies:

- PUBLIC: `EngineLibHeaderInterface`, `AnnoRefl`, `EngineDepGlm`
- PRIVATE: `EngineRender`, `EnginePhysics`, `EngineAssetCore`, `EngineRhi`, `EngineCore`, `EngineDepVulkan`, `EngineDepSdl`, `EngineDepJson`, `EngineDepImgui`, `tinyxml2`, `fastgltf`, `tinyobjloader`, `stb`, `ktx`, `meta_framework`

The `EngineLibFramework` OBJECT library target SHALL NOT exist.

#### Scenario: EngineFramework target exists
- **WHEN** the CMake configuration is generated
- **THEN** a target named `EngineFramework` exists and is a SHARED library
- **AND** no target named `EngineLibFramework` exists

#### Scenario: Build produces EngineFramework.dll
- **WHEN** `cmake --build --preset debug` completes
- **THEN** `EngineFramework.dll` exists in the build output directory
- **AND** the DLL was copied by the post-build step

#### Scenario: Framework links Rhi symbols directly
- **WHEN** `EngineFramework` is linked
- **THEN** symbols used by `PhysicsAdaptor.cpp` (e.g. `Rhi::DeviceContext`, `Rhi::SubmissionHelper`) resolve through the explicit PRIVATE `EngineRhi` link

### Requirement: MainClass is owned by Framework

`MainClass.h` and `MainClass.cpp` SHALL reside directly under `engine/Framework/` (no subdirectory), with the include path `<Framework/MainClass.h>`. `MainClass` SHALL carry `FRAMEWORK_API`. The `MainClass::GetInstance()` singleton SHALL remain, but no translation unit outside `engine/Framework/` or the terminal applications (tests, examples) SHALL include `Framework/MainClass.h` in engine module code; in particular no file under `engine/Render/` SHALL include it.

#### Scenario: MainClass located in Framework root
- **WHEN** the source tree is inspected
- **THEN** `engine/Framework/MainClass.h` and `engine/Framework/MainClass.cpp` exist
- **AND** no `MainClass.h` exists at the `engine/` root

#### Scenario: Render has no MainClass dependency
- **WHEN** every translation unit under `engine/Render/` is inspected
- **THEN** none of them includes `MainClass.h` or `Framework/MainClass.h`

### Requirement: Framework internal directory layout

The Framework module SHALL organize its sources into PascalCase directories:

- `Framework/` root: `MainClass.h`, `MainClass.cpp`
- `World/`: Scene, WorldSystem, Handle, EventQueue (moved from `world/`)
- `Object/`: GameObject (moved from `object/`)
- `Component/`: component base + TransformComponent/RenderComponent/physics components (moved from `component/`)
- `Scene/`: SceneAsset, LevelAsset
- `Import/`: GltfLoader, ObjLoader, Importer, ImportSharedUtil, ImportTypes, MaterialUtils, TextureImportUtils, UrdfLoader, UrdfTypes
- `Bridge/`: PhysicsAdaptor, PhysicsDescriptors, Internal (moved from `world/physics/`)
- `Input/`: Input
- `Tools/`: ComplexRenderGraphBuilder

#### Scenario: PascalCase directories exist
- **WHEN** the source tree is inspected
- **THEN** directories `World/`, `Object/`, `Component/`, `Import/`, `Bridge/`, `Input/`, and `Tools/` exist under `engine/Framework/`
- **AND** no lowercase `world/`, `object/`, or `component/` directories remain

#### Scenario: Import layer owned by Framework
- **WHEN** the source tree is inspected
- **THEN** `GltfLoader`, `ObjLoader`, `Importer`, `UrdfLoader`, and the import utilities exist under `engine/Framework/Import/`
- **AND** no loader or importer sources remain under `engine/Render/Loader/`

### Requirement: External asset import uses the AssetRuntime registry

`GltfLoader`, `ObjLoader`, and `UrdfLoader` constructors SHALL obtain the active `AssetManager` and `FileSystemDatabase` from `GetAssetRuntime()` instead of `MainClass::GetInstance()`. Their members SHALL be raw pointers (`AssetManager *m_asset_manager`, `FileSystemDatabase *m_database`) initialized from the registry; `m_database` SHALL be downcast from `AssetDatabase` via `dynamic_cast`. Asset construction SHALL remain `AssetManager::CreateAsset<T>()` followed by public-field population.

#### Scenario: Loader resolves services through AssetRuntime
- **WHEN** a `GltfLoader`, `ObjLoader`, or `UrdfLoader` is constructed after `MainClass` seeded `AssetRuntime`
- **THEN** its `m_asset_manager` and `m_database` members are obtained from `GetAssetRuntime()`
- **AND** `m_database` points to the `FileSystemDatabase` instance

#### Scenario: Loader guards against an unseeded registry
- **WHEN** a loader is constructed before `SetAssetRuntime` seeded a `FileSystemDatabase`
- **THEN** `m_database` is null and the load entry points detect the null pointer (throw or log-and-return) instead of dereferencing it

### Requirement: MeshAsset has no friend loaders

`MeshAsset` SHALL NOT declare `friend class ObjLoader` or any other loader class. Importers SHALL construct and populate assets exclusively through public members (`m_name`, `m_submeshes`, and their nested public fields).

#### Scenario: Friend declaration removed
- **WHEN** `engine/Render/Mesh/MeshAsset.h` is inspected
- **THEN** it contains no `friend` declaration
- **AND** `ObjLoader.cpp` still compiles against the public `MeshAsset` interface

### Requirement: Input lives in Framework and receives delta time explicitly

`Input` SHALL reside at `Framework/Input/Input.h` (include path `<Framework/Input/Input.h>`). `Input::Update(float delta_time)` SHALL take the frame delta time as a parameter and SHALL NOT fetch `TimeSystem` through `MainClass::GetInstance()`. `MainClass::RunOneFrame` SHALL call `input->Update(time->GetDeltaTime())`.

#### Scenario: Input update signature
- **WHEN** `Input::Update` is called from `RunOneFrame`
- **THEN** the delta time is passed as the `delta_time` argument
- **AND** no `MainClass::GetInstance()` call occurs inside `Input.cpp`

#### Scenario: Input reflection registered by Framework
- **WHEN** the `meta_framework` reflection parser runs
- **THEN** `Input.h` is scanned and its generated registration is compiled into `EngineFramework.dll`

### Requirement: Temporary render-graph builder is a Framework tool

`ComplexRenderGraphBuilder` SHALL reside under `Framework/Tools/` as a temporary utility, documented as a placeholder to be moved into the scripting system once it exists.

#### Scenario: Builder located in Tools
- **WHEN** the source tree is inspected
- **THEN** `ComplexRenderGraphBuilder.h/.cpp` exist under `engine/Framework/Tools/`

### Requirement: Per-DLL reflection registration for Framework

The Framework module SHALL be parsed by its own reflection task `meta_framework` (scanning all `engine/Framework/` headers, including `MainClass.h` and `Input.h`), exposing `extern "C" FRAMEWORK_API void RegisterFrameworkTypes()`. `MainClass` SHALL invoke `RegisterFrameworkTypes()` after `RegisterRenderTypes()` in the registration chain.

#### Scenario: Registration chain order
- **WHEN** `MainClass::Initialize` runs the reflection registration chain
- **THEN** the invocation order SHALL be `RegisterCoreTypes()`, `RegisterRhiTypes()`, `RegisterAssetCoreTypes()`, `RegisterPhysicsTypes()`, `RegisterRenderTypes()`, `RegisterFrameworkTypes()`

### Requirement: Framework export contract

`Framework/framework_export.h` SHALL define `FRAMEWORK_API` keyed on `FRAMEWORK_DLL_EXPORTS` (dllexport when building the module, dllimport otherwise, empty on non-Windows). Every public class in the Framework module SHALL be annotated, including at minimum: `MainClass`, `WorldSystem`, `Scene`, `GameObject`, `Component`, all components, `SceneAsset`, `LevelAsset`, `Handle`, `EventQueue`, `Input`, `PhysicsAdaptor`, `GltfLoader`, `ObjLoader`, `Importer`, and `ComplexRenderGraphBuilder`.

#### Scenario: Export macro exists and is applied
- **WHEN** the Framework headers are inspected
- **THEN** `framework_export.h` defines `FRAMEWORK_API`
- **AND** each public Framework class declaration carries `FRAMEWORK_API`
