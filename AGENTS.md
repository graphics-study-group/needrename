## Project Overview

C++20 game engine: Vulkan rendering, GPU physics, Python/libclang reflection, component-based framework.

## Build

**Toolchain**: MSYS2 CLANG64, Clang 22, target `x86_64-w64-windows-gnu`, Ninja generator. See `README.md` for dependencies.
If you can't find MSYS2, try to find clues from the configuration in the `.vscode` folder.

```sh
# Configure
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++

# Build
cmake --build build

# Test
cd build && ctest
```

**Env vars for building/running**: `MSYSTEM=CLANG64`, `PATH` prepended with `<msys2>/clang64/bin;<msys2>/usr/bin`. Debug builds need `VK_LAYER_PATH=<msys2>/clang64/bin` (gracefully skips if missing).

Reflection parser uses a virtualenv at `reflection_parser/parser_env/`.

## Architecture

```
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
