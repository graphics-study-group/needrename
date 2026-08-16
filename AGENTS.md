## Project Overview

C++20 game engine: Vulkan rendering, GPU physics, Python/libclang reflection, component-based framework.

## Build

**Toolchain**: MSYS2 CLANG64, Clang 22, target `x86_64-w64-windows-gnu`, Ninja generator. See `README.md` for dependencies.

Reflection parser uses a virtualenv at `build/parser_env/` (configurable via `ANROREFL_PARSER_ENV_DIR`).

Use the path that follows.

```sh
# Configure (debug or release)
cmake --preset debug

# Build
cmake --build --preset debug

# Test
ctest --preset debug
```

### CRITICAL: Environment Variables (Windows + MSYS2 CLANG64 only)

On Windows, when building with the MSYS2 CLANG64 toolchain, you **MUST** set environment variables (`MSYSTEM`, `PATH`, `VK_LAYER_PATH`) before any cmake, build, ctest, or executable command. Read the full instructions at `docs/build_instructions/windows_msys2_clang64.md`. On other platforms or toolchains, skip this section.

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
