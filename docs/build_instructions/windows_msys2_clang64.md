# Windows MSYS2 CLANG64 Build Instructions

This project uses the MSYS2 CLANG64 toolchain (`x86_64-w64-windows-gnu`) with the Ninja generator on Windows. Before running any cmake, build, ctest, or executable, you must set up the MSYS2 environment.

## Environment Variables

On Windows, when building with the MSYS2 CLANG64 toolchain, you **MUST** set these env vars in PowerShell before any cmake, build, ctest, or executable command:

```powershell
$env:MSYSTEM = "CLANG64"
$env:PATH = "<msys2_root>\clang64\bin;<msys2_root>\usr\bin;$env:PATH"
$env:VK_LAYER_PATH = "<msys2_root>\clang64\bin"
```

- `MSYSTEM=CLANG64` — selects the CLANG64 environment.
- `PATH` — must have `<msys2_root>/clang64/bin` and `<msys2_root>/usr/bin` **prepended** (before the existing `$env:PATH`), so the correct compilers and tools are found first.
- `VK_LAYER_PATH` — path to Vulkan validation layers for debug builds; the engine gracefully skips if missing at runtime.

## Finding `<msys2_root>`

`<msys2_root>` is the MSYS2 installation root directory. Find it in this order:

1. **Cached file** — check agent config dirs like `.copilot`, `.claude`, `.codex`, `.kilo` — whichever exists in the project root — for a `msys2_path.txt` file. Read the path from it.
2. **Common paths** — scan `C:\msys64` and `D:\msys64`.
3. **VSCode settings** — search `.vscode/settings.json` for `msys64`.
4. **Ask the user** — if all else fails, ask the user to provide the MSYS2 installation path.

## Caching the Path

Once `<msys2_root>` is found, save it to `<agent_config_dir>/msys2_path.txt` so future sessions can skip the search. The agent config dir is whichever of `.copilot` / `.claude` / `.codex` / `.kilo` (etc.) exists in the project root. This file must **NOT** be tracked by git.

## Dependencies

| Dependency | MSYS2 Package |
|---|---|
| Clang 22 (toolchain) | `mingw-w64-clang-x86_64-toolchain` |
| CMake | `mingw-w64-clang-x86_64-cmake` |
| Ninja | (included with CMake) |
| Python 3 | `mingw-w64-clang-x86_64-python` |
| Vulkan loader + headers | `mingw-w64-clang-x86_64-vulkan-loader` `mingw-w64-clang-x86_64-vulkan-headers` |
| Vulkan validation layers | `mingw-w64-clang-x86_64-vulkan-validation-layers` |
| glslang (shader compiler) | `mingw-w64-clang-x86_64-glslang` |
| SDL3 | `mingw-w64-clang-x86_64-sdl3` |
| LLDB (debugger) | `mingw-w64-clang-x86_64-lldb` `mingw-w64-clang-x86_64-lldb-mi` |
| Doxygen (optional) | `mingw-w64-clang-x86_64-doxygen` |

Other vendored dependencies (glm, SPIRV-Cross, imgui, etc.) are in the `third_party` directory and built automatically by CMake.

### One-Command Setup

```sh
pacman -S \
  mingw-w64-clang-x86_64-toolchain \
  mingw-w64-clang-x86_64-cmake \
  mingw-w64-clang-x86_64-python \
  mingw-w64-clang-x86_64-vulkan-loader \
  mingw-w64-clang-x86_64-vulkan-headers \
  mingw-w64-clang-x86_64-vulkan-validation-layers \
  mingw-w64-clang-x86_64-glslang \
  mingw-w64-clang-x86_64-sdl3 \
  mingw-w64-clang-x86_64-lldb \
  mingw-w64-clang-x86_64-lldb-mi
```

### Python interpreter for the reflection parser

The reflection parser uses a **user-provided** Python interpreter (no venv is auto-created). It must have `clang` and `mako` installed. Either use the MSYS2 Python (`mingw-w64-clang-x86_64-python`) with pip-installed requirements, or any other Python 3 environment:

```sh
python -m pip install -r third_party/AnnoRefl/parser/requirements.txt
```

CMake discovers the interpreter via `find_package(Python3)` (standard `Python3_EXECUTABLE` cache variable). If it lacks `clang`/`mako`, configuration fails with an actionable error message. To pin a specific interpreter (e.g. a personal venv), set `Python3_EXECUTABLE` in a gitignored `CMakeUserPresets.json`:

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "debug-user",
            "inherits": "debug",
            "cacheVariables": {
                "Python3_EXECUTABLE": "C:/path/to/python.exe"
            }
        }
    ]
}
```

## Build Steps

1. Clone the repository with submodules:

```sh
git clone --recursive <repo-url>
```

2. Configure with CMake. Make sure your shell has the CLANG64 environment active (`MSYSTEM=CLANG64`, and `clang64/bin` + `usr/bin` in `PATH`):

```sh
cmake --preset debug
```

3. Build:

```sh
cmake --build --preset debug
```

A `release` preset is also available. See `CMakePresets.json` for details.

## Runtime Environment

Before running any executable built from this project, the following environment variables are required:

| Variable | Value | Purpose |
|---|---|---|
| `PATH` | prepend `<msys2>/clang64/bin` and `<msys2>/usr/bin` | Find runtime DLLs (SDL3, Vulkan loader, libc++, etc.) |
| `VK_LAYER_PATH` | `<msys2>/clang64/bin` | Find Vulkan validation layers (Debug builds) |

Where `<msys2>` is your MSYS2 installation root (e.g. `C:\msys2`).

From PowerShell:

```powershell
$env:Path = "C:\msys2\clang64\bin;C:\msys2\usr\bin;$env:Path"
$env:VK_LAYER_PATH = "C:\msys2\clang64\bin"
./build/debug/bin/project_loading_test.exe
```

## VS Code Setup

The recommended VS Code extensions are:

- **CMake Tools** (`ms-vscode.cmake-tools`)
- **C/C++** (`ms-vscode.cpptools`)
- **CodeLLDB** (`vadimcn.vscode-lldb`) — for debugging with LLDB

Create `.vscode/settings.json` with the following content, adjusting paths to match your MSYS2 installation:

```jsonc
{
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "cmake.environment": {
        "MSYSTEM": "CLANG64",
        "PATH": "<msys2>\\clang64\\bin;<msys2>\\usr\\bin;${env:Path}",
        "VK_LAYER_PATH": "<msys2>\\clang64\\bin"
    },
    "cmake.generator": "Ninja",
    "C_Cpp.default.compilerPath": "<msys2>\\clang64\\bin\\clang++.exe",
    "cmake.debugConfig": {
        "type": "lldb",
        "program": "${command:cmake.launchTargetPath}",
        "cwd": "${workspaceFolder}",
        "env": {
            "PATH": "<msys2>\\clang64\\bin;<msys2>\\usr\\bin;${env:Path}",
            "VK_LAYER_PATH": "<msys2>\\clang64\\bin"
        }
    }
}
```

Replace `<msys2>` with your actual MSYS2 path (e.g. `C:\msys2`).
