# Windows MSVC Build Instructions

This project builds on Windows with the **MSVC** toolchain (Visual Studio 2022 or later, verified on **VS2026**) using the Visual Studio multi-config generator. The build tree lives under `build/msvc`. Before any cmake, build, ctest, or executable command, the dependencies below must be available.

## Dependencies

| Dependency | Provisioning | Purpose |
| --- | --- | --- |
| Visual Studio 2026 (min 2022) with **Desktop development with C++** | VS Installer | `cl.exe`, MSVC STL, Windows SDK |
| **C++ Clang tools for Windows** component | VS Installer (individual component, `VC\Tools\Llvm`) | `libclang.dll` for the reflection parser (clang 22 in VS2026) |
| CMake >= 3.31 (4.x for VS2026 generator) | VS bundled or https://cmake.org | Build system |
| Vulkan SDK >= 1.4.x | https://vulkan.lunarg.com/sdk/home | Headers (`vulkan.hpp`), loader, `glslangValidator`, validation layers |
| SDL3 | Official `SDL3-devel-<ver>-VC.zip` from https://github.com/libsdl-org/SDL/releases | Windowing / input |
| Python 3 + `.venv` | python.org or any Python 3 | Reflection parser interpreter (user-managed) |
| Doxygen (optional) | winget | Documentation generation |

No separate MinGW environment is required.

## Install Steps

### 1. Visual Studio

Install **VS2026** (Community or higher) with the **Desktop development with C++** workload and the **C++ Clang tools for Windows** individual component. The latter provides `VC\Tools\Llvm\x64\bin\libclang.dll` — the reflection parser **requires** it. If it is missing, CMake configure fails with an error naming the component.

### 2. Vulkan SDK

Install the LunarG Vulkan SDK (>= 1.4.x). The installer sets `VULKAN_SDK` and registers the loader; `glslangValidator` (used for shader compilation) is found automatically.

> Debug builds use Vulkan validation layers. The SDK installer does **not** set `VK_LAYER_PATH`; when layers are missing the engine skips them gracefully. If you want validation layers, set `VK_LAYER_PATH=<sdk>\Bin` before running a Debug executable.

### 3. SDL3 (official VC dev package)

Download `SDL3-devel-<ver>-VC.zip` from the SDL GitHub releases and extract it (e.g. to `D:\SDL3`). Set the **user** environment variable:

```powershell
SDL3_ROOT = D:\SDL3     # exact name; CMake honors <PackageName>_ROOT
```

Restart any open terminals / VS Code so the variable is visible. `find_package(SDL3 CONFIG REQUIRED COMPONENTS SDL3-shared)` resolves `SDL3Config.cmake` from `<root>/cmake/`. The build copies `SDL3.dll` next to the executables automatically.

### 4. Python interpreter for the reflection parser

The reflection parser uses a **user-provided** Python interpreter (no venv is auto-created). It must have `clang` (bindings only; the actual DLL comes from VS) and `mako` installed. Create a `.venv` in the project root:

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r third_party/AnnoRefl/parser/requirements.txt
```

The shared `msvc` preset sets `Python3_EXECUTABLE` to `${sourceDir}/.venv/Scripts/python.exe`. To use a different interpreter, override it in a gitignored `CMakeUserPresets.json`:

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "msvc-user",
            "inherits": "msvc",
            "cacheVariables": {
                "Python3_EXECUTABLE": "C:/path/to/python.exe"
            }
        }
    ]
}
```

> The pip `libclang` package also bundles an (older) libclang DLL, but the parser loads VS's clang 22 DLL via `LIBCLANG_LIBRARY_PATH` instead.

## Build Steps

The `msvc` presets use the **Visual Studio multi-config** generator: one build tree holds both configurations, and the output paths include the config name (`build/msvc/bin/Debug/`, `build/msvc/bin/Release/`). No Developer PowerShell / vcvars setup is needed — CMake locates Visual Studio itself.

```powershell
# Configure (Debug and Release live in the same build/msvc tree)
cmake --preset msvc

# Build Debug
cmake --build --preset msvc-debug

# Test Debug
ctest --preset msvc-debug

# Build Release
cmake --build --preset msvc-release
```

Equivalent direct commands: `cmake --build build/msvc --config Debug`, `ctest -C Debug`.
