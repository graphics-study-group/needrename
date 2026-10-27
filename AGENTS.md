## Project Overview

C++20 game engine: Vulkan rendering, GPU physics, Python/libclang reflection, component-based framework.

## Build

**Toolchains**: Windows = MSVC (Visual Studio 2026, multi-config generator); Linux = Clang + Ninja.

Platform setup (dependencies, environment variables, interpreters) is **NOT** in this file — see:
- Windows: `docs/build_instructions/windows_msvc.md`
- Linux (WSL2): `docs/build_instructions/linux.md`

These platforms may have the platform environment active **before any cmake, build, ctest, or executable command**. Read the relevant doc above first.

Reflection parser uses a user-provided Python interpreter (discovered via CMake's `Python3_EXECUTABLE`, e.g. a `.venv`).
No venv is auto-created.

On Linux:
```sh
# Configure (debug or release)
cmake --preset linux-debug   # or linux-release

# Build
cmake --build --preset linux-debug

# Test
ctest --preset linux-debug
```

On Windows (MSVC), use the multi-config presets:
```sh
cmake --preset msvc
cmake --build --preset msvc-debug   # or msvc-release
ctest --preset msvc-debug
```

## Architecture

```text
World (WorldSystem)
└── Scene (multiple, uint32_t ID)
    ├── GameObject (unique_ptr, via Scene::CreateGameObject)
    │   └── Component (unique_ptr, via Scene::CreateComponent)
    └── ...
```

- **Handles** (`ObjectHandle`/`ComponentHandle`): store `m_sceneID` + `m_ID`, resolve via `HandleResolver`. Never cache raw pointers across scenes.
- **Creation/deletion** queued, processed via `FlushCmdQueue()`.
- **Assets**: GUID-based, `AssetRef` with acquire/release, `AssetManager` with refcounting.
- **Reflection**: compile-time Python/libclang parser, generates code in `__generated__/meta_*`. Macros: `REFL_SER_CLASS`, `REFL_ENABLE`, `SER_ENABLE`. Whitelist (`REFL_WHITELIST`) / blacklist (`REFL_BLACKLIST`) modes.

## Code Style

Follow `CODE_STYLE.md` and `docs/CODE_STYLE_CN.md`. Format with `.clang-format`.

- Types/Functions: `PascalCase` · members: `m_snake_case` · locals: `snake_case` · constants: `ALL_CAPS`
- Namespace: `PascalCase` (except `detail`), anonymous namespace for file-local symbols
- Order: `public → protected → private`
- Doxygen: `@brief` + blank line + detail, then `@param`, then `@return`. No `@details`.

## Serialization Format

JSON with type tracking:

```json
{
    "%main_data": {
        "%type": "Engine::GameObject",
        "GameObject::m_handle": 1
    },
    "%extra_data": []
}
```
