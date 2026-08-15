# Tasks: Framework Top Module and Render DLL Split

Execution rule: after each numbered group below, the build and tests MUST be green, and work STOPS for user review — the user commits each group themselves. Commit messages in English.

## 1. File moves (Engine.dll still builds)

- [x] 1.1 `git mv engine/Render/Loader/*` → `engine/Framework/Import/` (GltfLoader, ObjLoader, Importer, ImportSharedUtil, ImportTypes, MaterialUtils + MaterialUtilsGltf, TextureImportUtils); update include paths in the moved files and their referrers
- [x] 1.2 Move `UrdfLoader`/`UrdfTypes` from `Framework/Loader/` into `Framework/Import/`; remove the empty `Loader/` directory
- [x] 1.3 `git mv` `ComplexRenderGraphBuilder.h/.cpp` → `engine/Framework/Tools/`; KEEP its `world_system` local (it resolves the active camera in the Main Lit pass — legal now that the file lives in Framework); add explicit includes (`CameraManager`, `ComputeStage`, `ShaderResourceBinding`, `RGAttachmentDesc`) lost from the Render PCH
- [x] 1.4 Remove `friend class ObjLoader;` from `engine/Render/Mesh/MeshAsset.h`
- [x] 1.5 Switch `GltfLoader`/`ObjLoader`/`UrdfLoader` constructors from `MainClass::GetInstance()` to `GetAssetRuntime()`; change their `weak_ptr` members to raw pointers (`AssetManager*`/`FileSystemDatabase*`) and rewrite `lock()`/`expired()` uses to direct pointer dereference (user decision)
- [x] 1.6 Move `fastgltf`/`tinyobjloader`/`stb` link deps from `EngineLibRender` to `EngineLibFramework`; keep `ktx` on Render
- [x] 1.7 `git mv` `GUISystem.h/.cpp` → `engine/Render/UserInterface/`; update includes (`<UserInterface/GUISystem.h>` → `<Render/UserInterface/GUISystem.h>` in MainClass and any referrer)
- [x] 1.8 `git mv` `Input.h/.cpp` → `engine/Framework/Input/`; add `float delta_time` parameter to `Input::Update`; remove the `MainClass.h` include from `Input.cpp`; pass `time->GetDeltaTime()` from `MainClass::RunOneFrame`
- [x] 1.9 `git mv` `MainClass.h/.cpp` → `engine/Framework/`; update every `#include <MainClass.h>` / `"MainClass.h"` to `<Framework/MainClass.h>` (Framework internals ~10, moved loaders 3, Tests 2, all example `main.cpp`, engine CMake source list)
- [x] 1.10 Unify Framework directory casing via `git mv`: `world/` → `World/`, `object/` → `Object/`, `component/` → `Component/`, `world/physics/` → `Bridge/`; update all include paths
- [x] 1.11 Verify: full build green, `ctest --preset debug` green (Engine.dll still produced). **STOP for user review + commit**

## 2. Cut Render's upward dependencies

- [x] 2.1 Create `engine/Render/RenderRuntime.h/.cpp`: `RenderRuntimeContext { RenderSystem* render_system; ShaderCompiler* shader_compiler; }` + `SetRenderRuntime`/`GetRenderRuntime`
- [x] 2.2 Seed the registry in `MainClass::Initialize` (after `RenderSystem` and editor-mode `ShaderCompiler` exist); clear it in `MainClass::~MainClass`
- [x] 2.3 Rewrite `RenderResourceHandle::~RenderResourceHandle` to resolve `GetRenderRuntime().render_system` instead of `MainClass::GetInstance()`; keep all null checks
- [x] 2.4 Add `virtual AssetPath GetAssetPath(GUID guid) const = 0` to `AssetDatabase`; mark `FileSystemDatabase::GetAssetPath` as `override`
- [x] 2.5 Rewrite `ShaderAsset::Compile`: source path via `GetAssetRuntime().asset_database->GetAssetPath(GetGUID())`; compiler via `GetRenderRuntime().shader_compiler`; log + return false when the compiler is absent
- [x] 2.6 Add `const std::filesystem::path &project_assets_path` parameter to `ShaderCompiler`'s constructor, forwarded to `DirStackFileIncluder::addSystemPath`; remove the `MainClass` query from `ShaderIncluder.cpp`; pass `FileSystemDatabase::GetProjectAssetsPath()` from `MainClass`
- [x] 2.7 Remove the dead `#include <MainClass.h>` (and unused `GUISystem.h` include) from `RenderSystem.cpp`
- [x] 2.8 Verify: grep confirms zero `MainClass.h`/`Framework/` includes under `engine/Render/`; build + ctest green. **STOP for user review + commit**

## 3. Reflection split

- [x] 3.1 `engine/Render/CMakeLists.txt`: add `meta_render` (GLOB `Render/*.h`, `filter_files_with_reflection_macros`, `add_reflection_parser` into `Render/__generated__`)
- [x] 3.2 `engine/Framework/CMakeLists.txt`: add `meta_framework` (GLOB `Framework/*.h` — includes `MainClass.h` and `Input.h`)
- [x] 3.3 Create `engine/Render/RenderReflectionRegistration.cpp` exposing `extern "C" void RegisterRenderTypes()` (export macro `RENDER_API` deferred to 4.1 — Render is still an OBJECT lib here)
- [x] 3.4 Create `engine/Framework/FrameworkReflectionRegistration.cpp` exposing `extern "C" void RegisterFrameworkTypes()` (export macro `FRAMEWORK_API` deferred to 5.1)
- [x] 3.5 Update `MainClass.cpp`: declare the two new registrars, call them in order Core → Rhi → AssetCore → Physics → Render → Framework (drops the deleted `meta_engine` `RegisterAllTypes`)
- [x] 3.6 Remove the `meta_engine` block from `engine/CMakeLists.txt` (GLOB/excludes/`filter_files_with_reflection_macros`/`add_reflection_parser`/`add_dependencies`)
- [x] 3.7 Verify: build + ctest green; serialization tests (`shader_refl_test` etc.) pass. **STOP for user review + commit**

## 4. Render DLL

- [ ] 4.1 Create `engine/Render/render_export.h` (`RENDER_API`, keyed on `RENDER_DLL_EXPORTS`)
- [ ] 4.2 Annotate ALL public Render classes with `RENDER_API` in one pass (RenderSystem, RenderSystem/ managers, present providers, RenderGraph/pipeline/CommandBuffer, all `*Asset`, resource managers + handle, ShaderCompiler, GUISystem)
- [ ] 4.3 Rework `engine/Render/CMakeLists.txt`: `EngineRender` SHARED with `RENDER_DLL_EXPORTS`; PUBLIC `EngineLibHeaderInterface`/`AnnoRefl`/`EngineDepGlm`; PRIVATE `EngineAssetCore`/`EngineRhi`/`EngineCore`/`EngineDepVulkan`/`EngineDepSdl`/`EngineDepImgui`/`glslang`/`ktx`/`stb`/`meta_render`; compile `$<TARGET_OBJECTS:imgui>` into the DLL
- [ ] 4.4 Update `engine/CMakeLists.txt`: drop `$<TARGET_OBJECTS:EngineLibRender>`, link `EngineRender` PUBLIC; move `add_dependencies(Engine shader)` to `EngineRender`; add `$<TARGET_FILE:EngineRender>` to POST_BUILD copy
- [ ] 4.5 Verify: build + ctest green; `EngineRender.dll` copied to output. **STOP for user review + commit**

## 5. Framework DLL (final split)

- [ ] 5.1 Create `engine/Framework/framework_export.h` (`FRAMEWORK_API`, keyed on `FRAMEWORK_DLL_EXPORTS`)
- [ ] 5.2 Annotate ALL public Framework classes with `FRAMEWORK_API` in one pass (MainClass, WorldSystem, Scene, GameObject, Component base + all components, SceneAsset, LevelAsset, Handle, EventQueue, Input, PhysicsAdaptor, GltfLoader, ObjLoader, Importer, ComplexRenderGraphBuilder)
- [ ] 5.3 Rework `engine/Framework/CMakeLists.txt`: `EngineFramework` SHARED with `FRAMEWORK_DLL_EXPORTS`; PUBLIC `EngineLibHeaderInterface`/`AnnoRefl`/`EngineDepGlm`; PRIVATE `EngineRender`/`EnginePhysics`/`EngineAssetCore`/`EngineRhi`/`EngineCore`/`EngineDepVulkan`/`EngineDepSdl`/`EngineDepJson`/`tinyxml2`/`fastgltf`/`tinyobjloader`/`stb`/`ktx`/`meta_framework`; compile `MainClass.cpp` into the DLL
- [ ] 5.4 Update `engine/CMakeLists.txt`: remove the `Engine` SHARED target; add `Engine` as INTERFACE aggregation linking the six module DLLs; finalize POST_BUILD list (drop `Engine`, add `EngineFramework`); delete `EngineLibExternalDependency`; update doxygen directory list (drop `UserInterface/`)
- [ ] 5.5 Verify: build + ctest green; `EngineFramework.dll`/`EngineRender.dll` copied; tests/examples link `Engine` unchanged; run one windowed example manually. **STOP for user review + commit**

## 6. Cleanup and regroup

- [ ] 6.1 Regroup Render internals with `git mv` only (no logic changes): asset classes → `Render/Asset/`, managers + handle + Memory → `Render/Resource/`, `Render/Pipeline/` + `Renderer/` → `Render/Pipeline/`, `RenderSystem/` stays, GUISystem in `UserInterface/`; update include paths
- [ ] 6.2 `git rm -r engine/UserInterface/` (empty after 1.7/1.8); confirm `EngineLibUserInterface` target and references are gone
- [ ] 6.3 Verify PCH (`FullRenderSystem.h`) still resolves; check `cmake_config.h.in` has no stale path references
- [ ] 6.4 Final verification: full clean build + `ctest --preset debug` green; `Engine.dll` no longer produced. **STOP for user review + commit**
