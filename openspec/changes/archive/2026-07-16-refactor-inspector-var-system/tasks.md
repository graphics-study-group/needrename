## 1. Var Inspector Core Layer

- [x] 1.1 Create `VarInspectorBase.h` in `editor/Editor/Inspector/` — abstract base with `virtual void Inspect(const std::string& name, Engine::Reflection::Var var) = 0`
- [x] 1.2 Create `VarInspectorRegistry.h` and `VarInspectorRegistry.cpp` — plain class (NOT singleton) with `Register(name, unique_ptr)` and `Find(name) -> VarInspectorBase*`
- [x] 1.3 Create `DefaultVarInspector.h` and `DefaultVarInspector.cpp` — `InspectVar()` dispatch function + `DefaultInspectVar()` handling int/float/bool/string/glm::vec3/glm::quat/enum/reflectable/unknown

## 2. Component Inspector Core Layer

- [x] 2.1 Create `ComponentInspectorBase.h` in `editor/Editor/Inspector/` — abstract base with `virtual void Inspect(Engine::Component& component) = 0`
- [x] 2.2 Create `ComponentInspectorRegistry.h` and `ComponentInspectorRegistry.cpp` — plain class (NOT singleton), same pattern as Var registry
- [x] 2.3 Create `DefaultComponentInspector.h` and `DefaultComponentInspector.cpp` — `InspectComponent()` dispatch function + `DefaultInspectComponent()` rendering TreeNode with field traversal via `InspectVar`

## 3. Engine-Specific Inspector Implementations (engine_impl/)

- [x] 3.1 Create `engine_impl/Inspector/HandleInspectors.h` and `HandleInspectors.cpp` — `ObjectHandleInspector` and `ComponentHandleInspector` implementing `VarInspectorBase`
- [x] 3.2 Create `engine_impl/Inspector/AssetInspector.h` and `AssetInspector.cpp` — `AssetRefInspector` implementing `VarInspectorBase`

## 4. Registration Entry Point

- [x] 4.1 Modify `engine_impl/InspectorRegistrations.h` — declare `RegisterAllVarInspectors(VarInspectorRegistry&)` and `RegisterAllComponentInspectors(ComponentInspectorRegistry&)` (receives registry references, no longer calls global `Get()`)
- [x] 4.2 Modify `engine_impl/InspectorRegistrations.cpp` — implement registrations via parameter references

## 5. Integrate into InspectorWidget

- [x] 5.1 Modify `InspectorWidget.h` — remove `InspectVar` private method declaration
- [x] 5.2 Modify `InspectorWidget.cpp` — replace `InspectVar` if-else chain with `InspectVar()` dispatcher call; replace inline Component field traversal with `InspectComponent()` dispatcher call; update includes

## 6. Wire Up Registration in Example

- [x] 6.1 Modify `example/editor_run_game_example/main.cpp` — add `#include <Editor/EditorMainClass.h>`, create `auto emc = Editor::EditorMainClass::GetInstance()`, call `emc->Initialize()` after `LoadProject()` and before `MainWindow` construction

## 7. Build Verification

- [x] 7.1 Build with `cmake --build build` and verify no compilation errors

## 8. EditorMainClass (Owns Registries)

- [x] 8.1 Create `editor/Editor/EditorMainClass.h` — singleton with `weak_ptr` + `once_flag` pattern, value members `VarInspectorRegistry` and `ComponentInspectorRegistry`, accessors returning references
- [x] 8.2 Create `editor/Editor/EditorMainClass.cpp` — `GetInstance()` implementation, `Initialize()` calling `RegisterAllVarInspectors` and `RegisterAllComponentInspectors` with member registries
- [x] 8.3 Update `DefaultVarInspector.cpp` — replace `VarInspectorRegistry::Get()` with `EditorMainClass::GetInstance()->GetVarInspectorRegistry()`
- [x] 8.4 Update `DefaultComponentInspector.cpp` — replace `ComponentInspectorRegistry::Get()` with `EditorMainClass::GetInstance()->GetComponentInspectorRegistry()`
