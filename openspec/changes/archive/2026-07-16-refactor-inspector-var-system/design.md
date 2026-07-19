## Context

The editor's `InspectorWidget` currently has a monolithic `InspectVar()` method — a ~100-line if-else chain dispatching by type name string comparison. Each branch renders ImGui controls for a specific type (int, float, bool, glm::vec3, etc.) and recursively handles reflected types. Engine-specific types (ObjectHandle, ComponentHandle, AssetRef) have their display logic inlined, coupling the Widget to engine internals. There is no way to add custom inspection logic without modifying the Widget source.

The reflection system provides `Engine::Reflection::Var` (type-erased pointer + `Type` metadata), `Type::GetAllFields()`, `Type::GetTypeKind()`, `EnumType::to_string/from_string`, etc. This metadata is sufficient to build a general-purpose inspection dispatcher.

## Goals / Non-Goals

**Goals:**
- Create a pluggable Var inspector system: register inspectors by type name, fall back to default reflection-based inspection
- Create a pluggable Component inspector system: register inspectors by Component type name, fall back to default field traversal
- Extract ObjectHandle, ComponentHandle, and AssetRef display logic into registered inspectors
- Keep the `Inspector/` tool layer free of engine-specific includes (no `Framework/`, `Asset/`, `MainClass.h`)
- Centralize type registration in a separate `engine_impl/` file called once from `main.cpp`
- Zero breaking changes to `InspectorWidget` public API

**Non-Goals:**
- Hot-reloading of inspector registrations
- Custom inspector serialization/deserialization
- Non-ImGui rendering backends
- Asset mode (`kInspectorModeAsset`) implementation — remains a placeholder
- Multi-scene handle resolution (current code only uses `GetMainSceneRef()`, new code preserves this)

## Decisions

### 1. Two-Layer Architecture: Var + Component

Two parallel but independent registry systems:

```
VarInspectorBase      → VarInspectorRegistry      → InspectVar() dispatcher
ComponentInspectorBase → ComponentInspectorRegistry → InspectComponent() dispatcher
```

**Why not one unified system?** Var inspection operates on `(name, Var)` pairs and is called recursively for struct fields. Component inspection operates on `Component&` and owns the entire TreeNode. Different granularity, different abstractions.

### 2. Boundary: `Inspector/` vs `engine_impl/`

```
Inspector/          — No engine headers allowed (only imgui.h, glm, Reflection/*)
engine_impl/        — May include engine headers (Handle.h, Scene.h, MainClass.h, AssetRef.h)
```

**Why?** The `Inspector/` layer is a pure tool that knows about reflection metadata and ImGui. It should not know about ObjectHandle resolution or Asset GUIDs. `engine_impl/` is where concrete engine type knowledge lives.

### 3. Registry Owned by EditorMainClass (NOT singleton)

The `VarInspectorRegistry` and `ComponentInspectorRegistry` are plain classes (no static `Get()` method). An `EditorMainClass` singleton owns them as value members, matching how `Engine::MainClass` owns its subsystems. This guarantees the registries live as long as the editor session — the caller in `main()` holds a `shared_ptr<EditorMainClass>`.

**Why not independent singletons?** The `weak_ptr` + `once_flag` pattern used by `Engine::MainClass` requires an external persistent `shared_ptr` holder. Without one, registries get destroyed after the first `Get()` call returns and their inspectors are lost. Making the registries members of `EditorMainClass` mirrors the engine's single-singleton architecture and guarantees correct lifetime.

```cpp
class EditorMainClass {
public:
    static std::shared_ptr<EditorMainClass> GetInstance();
    void Initialize();  // calls RegisterAllVarInspectors / RegisterAllComponentInspectors

    VarInspectorRegistry&       GetVarInspectorRegistry();
    ComponentInspectorRegistry&  GetComponentInspectorRegistry();
private:
    static std::weak_ptr<EditorMainClass> m_instance;
    static std::once_flag                m_instance_ready;
    VarInspectorRegistry       m_var_inspector_registry{};
    ComponentInspectorRegistry m_component_inspector_registry{};
};
```

**Usage in main.cpp:**
```cpp
auto emc = Editor::EditorMainClass::GetInstance();
emc->Initialize();  // ⇐ holds shared_ptr for entire editor lifetime
```

**Why `EditorMainClass` owns the registries rather than `MainWindow`?** `MainWindow` is an ImGui UI container — its responsibility is docking layout and widget management, not editor subsystem lifecycle. `EditorMainClass` is the editor counterpart of `Engine::MainClass`.

### 4. Default Fallback: Implicit, Not Registered

`DefaultInspectVar()` handles int, float, bool, std::string, glm::vec3, glm::quat, enum, and reflectable types. It is NOT a registered inspector — it's the else-branch when registry lookup fails.

**Why not register the default types?** These are the "everything else" catch-all. Registering them explicitly would mean every basic type needs a registration call, defeating the purpose of a default. The registry is for *overrides*, not the baseline.

### 5. Unknown Types: Silent Skip

When `DefaultInspectVar()` encounters a type that is neither a known basic type, nor an enum, nor reflectable — it renders nothing.

**Why?** The engine reflects many internal types that don't need editor UI. Showing "unknown type" warnings would create noise.

### 6. Component Inspectors Own the Full TreeNode

`ComponentInspectorBase::Inspect(Component&)` is responsible for the entire ImGui tree node (header + content), not just the interior. This gives custom inspectors full layout freedom (tabs, custom headers, non-collapsible sections).

### 7. ImGui PushID Convention

Callers are responsible for `ImGui::PushID/PopID`. Inspectors receive a stable ID context from the caller and may use additional `PushID` internally for their own child widgets.

### 8. Registration Function Location

`InspectorRegistrations.h/.cpp` lives in `engine_impl/` because it includes concrete inspector headers (`HandleInspectors.h`, `AssetInspector.h`) which depend on engine types. Registration functions now receive registry references as parameters instead of calling a global `Get()`:

```cpp
void RegisterAllVarInspectors(VarInspectorRegistry& registry);
void RegisterAllComponentInspectors(ComponentInspectorRegistry& registry);
```

These are called from `EditorMainClass::Initialize()`, which occurs after `MainClass::LoadProject()` (so scenes exist) and before `MainWindow` construction (so inspectors are ready when widgets render).

### 9. EditorMainClass Pattern

`EditorMainClass` follows the same `weak_ptr` + `once_flag` singleton pattern as `Engine::MainClass`. It is the **only** singleton in the Editor layer. All editor subsystems are accessed through it:

```cpp
// Editor layer callers:
auto& registry = EditorMainClass::GetInstance()->GetVarInspectorRegistry();
```

`main()` holds a `shared_ptr<EditorMainClass>` for the entire editor lifetime, guaranteeing all subsystems (registries, potential future additions) remain alive until shutdown.

## File Layout

```
editor/Editor/
├── EditorMainClass.h / .cpp                 ← NEW: Editor singleton, owns registries
├── Inspector/
│   ├── VarInspectorBase.h
│   ├── VarInspectorRegistry.h / .cpp
│   ├── DefaultVarInspector.h / .cpp          ← InspectVar() dispatcher + DefaultInspectVar()
│   ├── ComponentInspectorBase.h
│   ├── ComponentInspectorRegistry.h / .cpp
│   └── DefaultComponentInspector.h / .cpp    ← InspectComponent() dispatcher + DefaultInspectComponent()
│
├── engine_impl/
│   ├── InspectorRegistrations.h / .cpp
│   └── Inspector/
│       ├── HandleInspectors.h / .cpp         ← ObjectHandleInspector, ComponentHandleInspector
│       └── AssetInspector.h / .cpp           ← AssetRefInspector
│
└── Widget/
    └── InspectorWidget.h / .cpp              ← Modified to use new dispatchers
```

## Risks / Trade-offs

- **[Risk] Handle inspectors assume main scene**: Current ObjectHandle/ComponentHandle resolution uses `GetMainSceneRef()`. Multi-scene support would require passing scene context. → **Mitigation**: Preserve existing behavior. If multi-scene needed later, add a context parameter to `InspectVar` or let registered inspectors resolve their own context.
- **[Risk] Registry ordering**: If registration happens after first `InspectVar` call, unregistered types silently fall through. → **Mitigation**: Registration is called before `MainWindow` construction, before any rendering. Document this requirement.
- **[Trade-off] Single EditorMainClass singleton**: Global mutable state. → Acceptable because the `weak_ptr` + `once_flag` pattern is consistent with `Engine::MainClass`, and the instance is write-once-during-init, read-only during rendering. `main()` holds the persistent `shared_ptr`. No concurrent access.
