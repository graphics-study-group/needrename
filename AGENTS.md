## Project Overview

This project is a C++ game engine with:

- **Vulkan-based rendering** with multi-tier descriptor set architecture
- **Python-powered reflection/serialization** via libclang parsing at compile time
- **Component-based game framework** with World → Scene → GameObject/Component hierarchy

## Build Requirements

- **Compiler**: GCC only (not Clang/MSVC)
- **Standard**: C++20
- **Dependencies**: CMake, Python 3, Vulkan SDK 1.3+, SDL3
- **Build**: Out-of-source `mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug -G "MinGW Makefiles"`

## Core Architecture

### World / Scene / GameObject / Component

```
World (WorldSystem)
└── Scene (multiple, accessed via uint32_t ID)
    ├── GameObject (unique_ptr, created via Scene::CreateGameObject)
    │   └── Component (unique_ptr, created via Scene::CreateComponent)
    └── ...
```

- All objects use **Handles** (`ObjectHandle`, `ComponentHandle`) for references
- Handles store scene ID + object ID for safe cross-scene references
- Creation/deletion is **queued** and processed via `FlushCmdQueue()`

### Handles

- `ObjectHandle` / `ComponentHandle` contain `m_sceneID` + `m_ID`
- Resolve via `HandleResolver` to get actual pointers
- Never store raw pointers across Scene boundaries

### Asset System

- Assets identified by **GUID** (stored in asset files)
- `AssetRef` for runtime referencing with explicit acquire/release
- `AssetManager` handles loading/unloading with reference counting
- Serialization via `save_asset_to_archive` / `load_asset_from_archive`

### Reflection System

- Python parser (libclang) runs at **compile time**
- Generates code in `__generated__/meta_${target}` folders
- Macros: `REFL_SER_CLASS`, `REFL_ENABLE`, `SER_ENABLE`, etc.
- Whitelist mode: `REFL_SER_CLASS(REFL_WHITELIST)` - only annotated members
- Blacklist mode: `REFL_SER_CLASS(REFL_BLACKLIST)` - exclude marked members

## Code Style

- **Format**: Use `.clang-format` (run `clang-format -i <file>` before commit)
- **Naming**:
  - Types/Functions: PascalCase (`GameObject`, `GetTransform`)
  - Member variables: `m_` prefix + snake\_case (`m_position`)
  - Local variables: snake\_case (`delta_time`)
  - Constants/Macros: ALL\_CAPS
- **Namespace**: lowercase (`namespace Engine`)
- **Order**: public → protected → private

## Key Directories

| Directory            | Purpose                                                       |
| -------------------- | ------------------------------------------------------------- |
| `engine/`            | Core engine code (Asset, Core, Framework, Reflection, Render) |
| `editor/`            | Editor application                                            |
| `reflection_parser/` | Python parser for C++ reflection code generation              |
| `shader/`            | GLSL shader sources                                           |
| `test/`              | Test executables                                              |
| `third_party/`       | External dependencies (glm, SPIRV-Cross)                      |
| `docs/`              | Contributing guidelines, code style                           |
| `wiki/`              | Technical documentation                                       |

## Serialization Format

JSON-based with type tracking:

```json
{
    "%main_data": {
        "%type": "Engine::GameObject",
        "GameObject::m_handle": 1
    }
}
```

<br />

