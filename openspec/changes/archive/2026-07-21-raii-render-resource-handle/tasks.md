## 1. Template conversion — RenderResourceHandle.h

- [x] 1.1 Convert `RenderResourceHandle` struct to class template `RenderResourceHandle<ResourceType>`, keeping `index`, `generation`, `is_acquired` data members. Declare destructor (no body in header).
- [x] 1.2 Implement custom copy constructor: copy `index`/`generation`, force `is_acquired = false`.
- [x] 1.3 Implement custom move constructor: transfer all fields, reset source to `index = 0xFFFFFFFFu, generation = 0, is_acquired = false`.
- [x] 1.4 Implement copy-and-swap `operator=(RenderResourceHandle other)`.
- [x] 1.5 Update three typed handle structs: `MaterialInstanceHandle : public RenderResourceHandle<MaterialInstance>`, etc.

## 2. Fix initial refcount bug — IRenderResourceManager.inl

- [x] 2.1 Change `record.refcount = 1` to `record.refcount = 0` in `IRenderResourceManager::Create()`.

## 3. Manager destructor notification — IRenderResourceManager.h / RenderSystem

- [x] 3.1 Add destructor declarations to derived manager classes (`MaterialInstanceManager`, `MaterialLibraryManager`, `StaticMeshResourceManager`). Implement in .cpp calling `m_system.NotifyResourceManagerDestroyed(this)`.
- [x] 3.2 Add `NotifyResourceManagerDestroyed(void *ptr)` non-template method to `RenderSystem` that nullifies the matching tuple entry.
- [x] 3.3 Change `GetRenderResourceManager<T>()` return type from `T&` to `T*`.

## 4. Destructor implementation — new RenderResourceHandle.cpp

- [x] 4.1 Create `engine/Render/Resource/RenderResourceHandle.cpp` with out-of-line template destructor definition using defensive null chain: `is_acquired` → `MainClass::GetInstance()` → `GetRenderSystem()` → `GetRenderResourceManager<ManagerType>()` → `Release()`.
- [x] 4.2 Add explicit template instantiations for `MaterialInstance`, `MaterialLibrary`, `StaticMeshResource`.
- [x] 4.3 Add `RenderResourceHandle.cpp` to `engine/CMakeLists.txt`. (N/A — GLOB_RECURSE auto-detects)

## 5. Update all GetRenderResourceManager call sites

- [x] 5.1 `RendererManager.cpp` (5 call sites): `auto &mg` → `auto *mg`, `mg.Foo()` → `mg->Foo()`.
- [x] 5.2 `SceneDataManager.cpp` (2 call sites): same pattern.
- [x] 5.3 `MaterialInstanceManager.cpp` (2 call sites): same pattern.
- [x] 5.4 `CommandBuffer.cpp` (1 call site): same pattern.
- [x] 5.5 `LevelAsset.cpp` (1 call site): same pattern.
- [x] 5.6 Test files (~7 files): `shadow_map_test.cpp`, `skybox_test.cpp`, `mrt_test.cpp`, `new_material_test.cpp`, `pbr_test.cpp` (2 sites), `complex_mesh_test.cpp`.

## 6. Remove redundant manual release

- [x] 6.1 In `MaterialInstance::~MaterialInstance()` (`MaterialInstance.cpp:112-114`), remove `m_system.GetRenderResourceManager<MaterialLibraryManager>().Release(m_library)` (and the braces if it was the only statement).

## 7. Build and validate

- [x] 7.1 Run `cmake --build --preset debug` and fix any compilation errors.
- [x] 7.2 Run `ctest --preset debug` to verify `project_loading_test` passes without assertion failures.
- [x] 7.3 Run `cmake --build --preset release` to verify release build compiles.
