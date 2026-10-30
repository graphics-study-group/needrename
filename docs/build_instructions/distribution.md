# Installable Distribution and SDK

`cmake --install` produces a self-contained prefix whose executables run without
environment variables, working-directory assumptions, or developer-machine paths.
The same prefix is a CMake SDK other projects can build against.

## Install

Build the configuration you want to ship, then install it:

```powershell
# Windows (multi-config): one install per configuration
cmake --preset msvc
cmake --build --preset msvc-release
cmake --install build/msvc --config Release --prefix install
```

```sh
# Linux (single-config)
cmake --preset linux-release
cmake --build --preset linux-release
cmake --install build/linux-release --prefix install
```

`--prefix` defaults to the `install/` directory of the checkout (the presets set
`CMAKE_INSTALL_PREFIX`); pass another location to install elsewhere. A
multi-config install always installs the artifacts of the selected `--config`.

### Components

| Component | Content |
| --- | --- |
| `runtime` | Executables, every shared library they load (engine modules, reflection, imgui, ktx, fastgltf, glslang, SDL3), the built-in asset database, physics SPIR-V |
| `apps-data` | Project templates (`share/needrename/projects/`) |
| `examples-data` | Sample content used by the examples (`share/needrename/assets/`, without documentation screenshots) |
| `test-data` | Test executables and fixtures — only when configured with `-DENGINE_INSTALL_TESTS=ON` (default `OFF`) |
| `sdk` | Headers, import/static libraries, the CMake package, the reflection parser and type lists |

Install a subset with `--component`:

```powershell
cmake --install build/msvc --config Release --prefix install --component runtime
```

A `runtime`-only install is a working engine distribution. Executables that also
need example data (`projects/`, `assets/`) fail with a diagnostic naming the
missing directory unless `apps-data`/`examples-data` are installed too.

### Installed layout

```text
<prefix>/bin/                          executables + all shared libraries
<prefix>/share/needrename/
    builtin_assets/                    built-in asset database (shader form per ENGINE_SHADER_MODE)
    projects/                          project templates                 (apps-data)
    assets/                            sample content, no img/           (examples-data)
    spirv/                             physics compute SPIR-V, source-relative paths
    reflection/parser/                 reflection parser scripts         (sdk)
    reflection/type_lists/*.pkl        engine known-type lists           (sdk)
    test_data/                         test fixtures                     (test-data only)
<prefix>/include/needrename/           engine, editor, reflection and vendored headers
<prefix>/include/glslang/              glslang's own install interface
<prefix>/lib/                          import/static libraries + lib/cmake/needrename
```

The Vulkan loader and driver are **not** bundled; they must be provided by the
machine (Vulkan SDK).

### Shader form: `ENGINE_SHADER_MODE`

Built-in shaders are staged per configuration and installed in the selected form:

| Value | Built-in shaders |
| --- | --- |
| `auto` (default) | `glsl` for Debug builds, `spv` for every other configuration |
| `glsl` | GLSL sources plus the shader include tree, compiled at runtime |
| `spv` | Shader archives with embedded SPIR-V; no GLSL sources, no include tree |

```powershell
cmake --preset msvc -DENGINE_SHADER_MODE=spv
```

`ENGINE_SHADER_MODE=spv` requires `ENGINE_BUILD_TOOLS=ON` (default), because the
build-time `shader_baker` helper performs the baking. User-provided `.glsl`
shaders are still compiled at runtime in every mode.

### Runtime data root

Executables resolve one data root, first match wins:

1. `NEEDRENAME_DATA_ROOT`, when it names an existing directory;
2. `<executable directory>/../share/needrename` (the installed layout);
3. a compile-time developer fallback used by the build tree.

Set the override for unusual deployments and test harnesses:

```powershell
$env:NEEDRENAME_DATA_ROOT = "$PWD\install\share\needrename"
```

A missing data root or missing required subdirectory (for example installing
`runtime` only) fails with an error naming the resolved path and the resolution
step that produced it.

### ZIP package

With `-DENGINE_PACKAGE_ZIP=ON` (default) CPack produces a ZIP of the install
layout:

```powershell
cd build/msvc
cpack -C Release           # -> needrename-<version>-windows.zip
```

Unpacking the archive yields the same layout as `cmake --install`.

## Consuming the SDK

Configure with the install prefix on the CMake search path (and with SDL3 and
the Vulkan SDK discoverable, exactly as for building the engine):

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_game CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(needrename CONFIG REQUIRED)

add_executable(my_game main.cpp)
target_link_libraries(my_game PRIVATE needrename::Engine)
```

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD\install"
cmake --build build
```

Exported targets use the `needrename::` namespace: `needrename::Engine` (the
aggregate the in-tree examples link), the module libraries (`needrename::EngineCore`,
`needrename::EngineFramework`, ...), `needrename::AnnoRefl`, `needrename::EngineEditor`
and the vendored dependencies. `SDL3`, `Vulkan` and `Threads` are resolved on the
consumer machine by the package config.

At run time the engine's shared libraries must be findable: add `<prefix>/bin` to
`PATH` on Windows; installed libraries and executables use an `$ORIGIN`-relative
RPATH on Linux, so no `LD_LIBRARY_PATH` is needed. An application that lives
outside `<prefix>/bin` should point `NEEDRENAME_DATA_ROOT` at
`<prefix>/share/needrename`.

### Reflection for consumer components

`needrename_add_reflection()` runs the shipped parser over the consumer's headers
at build time and adds the generated registration/serialization code to the
target:

```cmake
add_executable(my_game main.cpp Component.cpp)
target_link_libraries(my_game PRIVATE needrename::Engine needrename::json)

needrename_add_reflection(my_game
    HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/Component.h")
```

Optional arguments:

- `INCLUDE_DIRS <dir>...` — parser include search paths; defaults to the target's
  own include directories (including those from linked engine targets).
- `TYPE_LISTS <pkl>...` — engine known-type lists; defaults to every list
  installed in `share/needrename/reflection/type_lists/`.
- `GENERATED_DIR <dir>` — where the generated archive code is written; defaults
  to `<build>/__generated__/<target>`.

In the source:

```cpp
// Component.h
#include <AnnoRefl/macros.h>
#include <Framework/Component/Component.h>

class REFL_SER_CLASS(REFL_WHITELIST) Spinner : public Engine::Component {
    REFL_SER_BODY_OVERRIDE(Spinner)
public:
    REFL_SER_ENABLE float m_speed = 10.0f;
};

// Component.cpp (last line)
#include "__generated__/Component.h.inc"
```

```cpp
// main.cpp
#include <AnnoRefl/reflection.h>
#include <meta_my_game/reflection_init.inc>   // registrars for the consumer types

int main() {
    AnnoRefl::Initialize();
    RegisterAllTypes();
    // ... create/inspect the game world
}
```

Types derived from engine types are accepted because the engine's type lists
ship with the SDK — the parser does not rescan engine headers. When the
application does not go through `MainClass::Initialize`, register the engine
types it uses first: `RegisterCoreTypes()`, `RegisterAssetCoreTypes()`,
`RegisterRhiTypes()`, `RegisterPhysicsTypes()`, `RegisterRenderTypes()` and
`RegisterFrameworkTypes()` are exported (C linkage) from the module
libraries.

Reflection generation needs Python 3 with the `clang` and `mako` packages and a
libclang shared library on the consumer machine. Point CMake at the interpreter
with `-DPython3_EXECUTABLE=...` and, if the parser cannot locate libclang, set
`LIBCLANG_LIBRARY_FILE` to the shared library path (on Windows the parser uses
Visual Studio's bundled libclang automatically).

### Verifying an installation

[`example/sdk_consumer/`](../example/sdk_consumer/README.md) is a standalone
project that uses nothing but the installed prefix, and doubles as the check that
the installation is complete and self-sufficient. It is not an in-tree build
target; build it by hand into its own build directory:

```powershell
cmake -S example\sdk_consumer -B example\sdk_consumer\build -G "Visual Studio 18 2026"
cmake --build example\sdk_consumer\build --config Release
```

The project defaults to the `install/` prefix of the checkout that contains it
and to that checkout's `.venv`; a different prefix or interpreter is passed as
`-DCMAKE_PREFIX_PATH=<prefix>` / `-DPython3_EXECUTABLE=<python>`.

It builds two programs: a minimal consumer that resolves the data root from its
own location (copy it into `<prefix>/bin` and run it without
`NEEDRENAME_DATA_ROOT`), checks that `builtin_assets/`, `projects/` and `assets/`
exist and reports the installed shader form; and a reflection consumer whose
component derives from `Engine::Transform`, generated by the shipped parser, and
round-tripped through the archive format. The README lists the exact run commands
and the expected output.

The build configuration must match the installed export: a Release install ships
`needrenameTargets-release.cmake` and rejects a Debug consumer.

