# render-module

## Purpose

Define the Render module as a standalone shared library `EngineRender.dll`: the render system and its managers, the render pipeline, render asset classes, resource managers, and `GUISystem` (as `Render/UserInterface/`). The module is free of `MainClass`/`Framework` dependencies and provides the `RenderRuntimeContext` service registry plus the shader compile service contract. Internally it is grouped by lifecycle: `Asset/`, `Resource/`, `Pipeline/`, `RenderSystem/`.

## Requirements

### Requirement: Render builds as EngineRender shared library

The Render module SHALL build as a shared library target named `EngineRender` producing `EngineRender.dll`, compiled from all sources under `engine/Render/`. The target SHALL define `RENDER_DLL_EXPORTS` privately and SHALL compile the imgui OBJECT objects (`$<TARGET_OBJECTS:imgui>`) into the DLL.

The target SHALL declare the following dependencies:

- PUBLIC: `EngineLibHeaderInterface`, `AnnoRefl`, `EngineDepGlm`
- PRIVATE: `EngineAssetCore`, `EngineRhi`, `EngineCore`, `EngineDepVulkan`, `EngineDepSdl`, `EngineDepImgui`, `glslang`, `ktx`, `stb`, `meta_render`, `shader`

The `EngineLibRender` OBJECT library target SHALL NOT exist.

#### Scenario: EngineRender target exists
- **WHEN** the CMake configuration is generated
- **THEN** a target named `EngineRender` exists and is a SHARED library
- **AND** no target named `EngineLibRender` exists

#### Scenario: Build produces EngineRender.dll with imgui
- **WHEN** `cmake --build --preset debug` completes
- **THEN** `EngineRender.dll` exists in the build output directory
- **AND** the imgui symbols resolve inside the DLL

### Requirement: Render has no MainClass or Framework dependencies

No translation unit under `engine/Render/` SHALL include `MainClass.h`, `Framework/MainClass.h`, or any `Framework/...` header. All previous usages SHALL be replaced as follows:

- `ShaderAsset` resolves the asset database through `GetAssetRuntime()` and the compiler through the render runtime registry
- `ShaderIncluder` receives include paths at construction
- `RenderResourceHandle` resolves `RenderSystem` through the render runtime registry
- the dead include in `RenderSystem.cpp` is removed
- `GltfLoader`/`ObjLoader`/`ComplexRenderGraphBuilder` moved to the Framework module

#### Scenario: Compile-time independence
- **WHEN** every translation unit under `engine/Render/` is inspected
- **THEN** no include directive referencing `MainClass.h` or `Framework/` is present

### Requirement: RenderRuntimeContext service registry

The Render module SHALL provide a module-level registry (`RenderRuntimeContext`) with `SetRenderRuntime`/`GetRenderRuntime` functions storing pointers to the active `RenderSystem` and, in editor builds, the `ShaderCompiler`.

#### Scenario: MainClass seeds the registry
- **WHEN** `MainClass::Initialize` constructs the `RenderSystem` (and `ShaderCompiler` in editor mode)
- **THEN** it calls `SetRenderRuntime({renderer.get(), shader_compiler.get()})` once

#### Scenario: Registry cleared at teardown
- **WHEN** `MainClass` shuts down
- **THEN** `SetRenderRuntime({})` clears the registry pointers

#### Scenario: Handle destruction resolves RenderSystem through the registry
- **WHEN** an acquired `RenderResourceHandle` is destroyed while the registry is seeded
- **THEN** its destructor obtains the `RenderSystem` from `GetRenderRuntime().render_system` and calls `Release()` on the typed manager

#### Scenario: Handle destruction after registry cleared
- **WHEN** an acquired `RenderResourceHandle` is destroyed after `SetRenderRuntime({})`
- **THEN** the destructor SHALL return without calling `Release()` (accepting the leak at process exit)

### Requirement: Shader compile service contract

`ShaderAsset::Compile()` SHALL obtain the compiler from the render runtime registry (`GetRenderRuntime().shader_compiler`). When the pointer is null, `Compile()` SHALL log an error and return false. `ShaderCompiler`'s constructor SHALL accept `const std::filesystem::path &project_assets_path` and SHALL register it as a system include path on its `DirStackFileIncluder`; `DirStackFileIncluder` SHALL NOT query `MainClass`.

#### Scenario: Editor mode compiles through the registry
- **WHEN** a GLSL `ShaderAsset` loads in editor mode
- **THEN** `Compile()` resolves the non-null compiler from the registry and compiles successfully

#### Scenario: No compiler registered
- **WHEN** `Compile()` runs while the registry has no compiler
- **THEN** it SHALL log an error and return false without crashing

#### Scenario: Source path resolved through AssetDatabase interface
- **WHEN** `ShaderAsset::Compile()` has no absolute path and resolves its source location
- **THEN** it SHALL call `GetAssetRuntime().asset_database->GetAssetPath(GetGUID())` and SHALL NOT downcast the database to `FileSystemDatabase`

### Requirement: GUISystem belongs to Render

`GUISystem` SHALL reside at `Render/UserInterface/GUISystem.h` (include path `<Render/UserInterface/GUISystem.h>`). The `UserInterface/` top-level directory and the `EngineLibUserInterface` target SHALL NOT exist.

#### Scenario: GUISystem located in Render
- **WHEN** the source tree is inspected
- **THEN** `engine/Render/UserInterface/GUISystem.h` and `GUISystem.cpp` exist
- **AND** no `engine/UserInterface/` directory exists

### Requirement: Render internal layout

After the module split, the Render module SHALL group its sources by lifecycle (this regrouping happens as a pure `git mv` step with no logic changes):

- `Asset/`: Mesh/Texture/Material/Shader asset classes plus ShaderCompiler/ShaderIncluder
- `Resource/`: resource managers, RenderResourceHandle, Memory (RenderTargetTexture)
- `Pipeline/`: Pipeline (RenderGraph, CommandBuffer, MaterialInstance) plus Renderer (Camera, geometry)
- `RenderSystem/`: FrameManager, SceneDataManager, CameraManager, RendererManager, present providers
- `UserInterface/`: GUISystem
- root: RenderSystem.h, FullRenderSystem.h, AttachmentUtils, render_export.h

#### Scenario: Lifecycle directories exist
- **WHEN** the regrouping step completes
- **THEN** directories `Asset/`, `Resource/`, `Pipeline/`, `RenderSystem/`, and `UserInterface/` exist under `engine/Render/`
- **AND** the flat `Mesh/`, `Texture/`, `Material/`, `Shader/`, `Memory/`, `Renderer/` directories are gone

### Requirement: Per-DLL reflection registration for Render

The Render module SHALL be parsed by its own reflection task `meta_render` (scanning all `engine/Render/` headers), exposing `extern "C" RENDER_API void RegisterRenderTypes()`. `meta_engine` SHALL NOT exist, and `MainClass` SHALL invoke `RegisterRenderTypes()` before `RegisterFrameworkTypes()`.

#### Scenario: meta_render exists, meta_engine gone
- **WHEN** the CMake configuration is generated
- **THEN** targets `meta_render` and `meta_framework` exist
- **AND** no target named `meta_engine` exists

### Requirement: Render export contract

`Render/render_export.h` SHALL define `RENDER_API` keyed on `RENDER_DLL_EXPORTS`. Every public class in the Render module SHALL be annotated, including at minimum: `RenderSystem`, all `RenderSystem/` managers, present providers, `RenderGraph` and pipeline classes, `CommandBuffer`, all render asset classes, resource managers, `RenderResourceHandle`, `ShaderCompiler`, and `GUISystem`.

#### Scenario: Export macro exists and is applied
- **WHEN** the Render headers are inspected
- **THEN** `render_export.h` defines `RENDER_API`
- **AND** each public Render class declaration carries `RENDER_API`
