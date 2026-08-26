> 🇨🇳 [中文版](./docs/README_CN.md) | 🇺🇸 English

An unnamed game engine with GPU-accelerated physics simulation, Vulkan-based rendering, Python-powered reflection/serialization, and a flexible component-based game framework.

![engine_editor](./assets/img/engine_editor.png)

## Building the Engine

We recommend using **MSYS2 CLANG64** subsystem to build projects and manage dependency packages.

### Dependencies

| Dependency | MSYS2 Package |
|---|---|
| Clang 22 (toolchain) | `mingw-w64-clang-x86_64-toolchain` |
| CMake | `mingw-w64-clang-x86_64-cmake` |
| Ninja | (included with CMake) |
| Python 3 | `mingw-w64-clang-x86_64-python` |
| Python pip (for Python 3) | `mingw-w64-clang-x86_64-python-pip` |
| Vulkan loader + headers | `mingw-w64-clang-x86_64-vulkan-loader` `mingw-w64-clang-x86_64-vulkan-headers` |
| Vulkan validation layers | `mingw-w64-clang-x86_64-vulkan-validation-layers` |
| glslang (shader compiler) | `mingw-w64-clang-x86_64-glslang` |
| SDL3 | `mingw-w64-clang-x86_64-sdl3` |
| LLDB (debugger) | `mingw-w64-clang-x86_64-lldb` `mingw-w64-clang-x86_64-lldb-mi` |
| Doxygen (optional) | `mingw-w64-clang-x86_64-doxygen` |

Other vendored dependencies (glm, SPIRV-Cross, imgui, etc.) are in the `third_party` directory and built automatically by CMake.

#### One-Command Setup

```sh
pacman -S \
  mingw-w64-clang-x86_64-toolchain \
  mingw-w64-clang-x86_64-cmake \
  mingw-w64-clang-x86_64-python \
  mingw-w64-clang-x86_64-python-pip \
  mingw-w64-clang-x86_64-vulkan-loader \
  mingw-w64-clang-x86_64-vulkan-headers \
  mingw-w64-clang-x86_64-vulkan-validation-layers \
  mingw-w64-clang-x86_64-glslang \
  mingw-w64-clang-x86_64-sdl3 \
  mingw-w64-clang-x86_64-lldb \
  mingw-w64-clang-x86_64-lldb-mi
```

### Python Environment (Reflection Parser)

The reflection parser runs on Python and needs `libclang` and `mako`. Set up a **user-managed** virtual environment once before configuring (see the [Windows build instructions](./docs/build_instructions/windows_msys2_clang64.md) for details):

```sh
python -m venv .venv
.venv/bin/python -m pip install -r third_party/AnnoRefl/parser/requirements.txt
```

The CMake presets point `Python3_EXECUTABLE` at this `.venv`.

### Build Steps

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

### Runtime Environment

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

### VS Code Setup

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

## Project Structure

```
docs/                     # Contribution guidelines and code style
wiki/                    # Technical documentation
assets/                  # Raw resources
builtin_assets/          # Built-in assets used across all projects
editor/                  # Engine editor code
engine/
    Asset/               # Asset core infrastructure (Asset, AssetRef, AssetManager, AssetDatabase)
    Core/                # Core features (Math, Functional)
    Framework/           # Top-level orchestrator: MainClass, World/Scene/GameObject/Component, asset import, Input
    Physics/             # GPU-accelerated physics engine
    Render/              # Vulkan rendering systems, render assets, and GUISystem
    Rhi/                 # GPU abstraction layer (buffers, textures, pipelines, submission)
example/                 # Runnable game examples
    physics_example/     # Physics simulation demo
projects/                # Example game projects
test/                    # Test executables
third_party/             # External dependencies (glm, SPIRV-Cross, AnnoRefl)
    AnnoRefl/            # Reflection/serialization runtime and Python parser
```

## Build Targets

The engine is split into modular shared libraries (DLLs) with one-way dependencies, aggregated by a single `Engine` INTERFACE target that consumers link against:

- **AnnoRefl** — Reflection/serialization runtime and Python parser (`third_party/AnnoRefl`)
- **EngineCore** — Math and functional utilities
- **EngineRhi** — GPU abstraction layer (buffers, textures, pipelines, submission)
- **EngineAssetCore** — Asset core infrastructure (Asset, AssetRef, AssetManager, AssetDatabase)
- **EnginePhysics** — GPU-accelerated physics engine
- **EngineRender** — Vulkan rendering systems, render assets, and GUISystem
- **EngineFramework** — Top-level orchestrator (MainClass, World/Scene/GameObject/Component, asset import, Input)
- **EngineEditor** — Engine editor, loaded by the editor example executable
- **tests** — Executable demos and test cases (runnable via CTest)

All executables and DLLs are written to a unified `bin/` output directory (import libraries to `lib/`).

## Key Features

### 1. GPU Physics Simulation

![physics_example1](assets/img/physics_example1.gif) ![physics_example2](assets/img/physics_example2.gif)

- **XPBD Solver** — GPU-accelerated position-based dynamics with sub-step integration, per-step collision detection, and Jacobi position/velocity constraint solving
- **Collision Detection Pipeline** — Spatial-hash broad-phase with AABB overlap pruning, followed by MPR-based narrow-phase contact generation with plane fitting and rotating calipers manifold reduction
- **Collision Shapes** — Box, sphere, and cylinder primitives with per-type inertia functions and generic `feature` vec3 interface
- **Joint Constraints** — Fixed joints (distance-locked relative pose) and hinge joints (single-axis rotation with configurable limits), solved as XPBD constraints on the GPU
- **Rigid Body Dynamics** — Gravity, force/torque integration, linear/angular damping, dynamic/kinematic types, friction and restitution
- **GPU Parallel Algorithms** — Reusable compute modules: work-efficient parallel prefix scan, 8-bit LSD radix sort, and deduplication-compaction for sorted arrays
- **Physics Components** — `RigidBodyComponent`, `CollisionShapeComponent`, and `PhysicsConstraintComponent` integrate with the GameObject framework; shapes auto-attach to ancestor rigid bodies
- **Scene Builder** — Declarative `SceneBuilder` API (`AddBox`, `AddSphere`, `AddCylinder`, `AddDoublePendulum`) for rapid physics scene construction

### 2. Vulkan Rendering System

- Multi-tier descriptor set architecture for uniforms
- Frame-in-flight optimized buffer management
- JSON-defined materials with shader pipeline configuration
- Automatic descriptor set allocation and binding
- Push constant support for efficient matrix updates
- Independent render graphs for physics and rendering subsystems

### 3. Advanced Reflection & Serialization

- Python-powered C++ header parsing for runtime type information
- Automatic generation of reflection metadata during compilation
- Dynamic class instantiation, method invocation, and property access
- Customizable serialization with STL container and smart pointer support
- JSON-based serialization format with object relationship tracking

### 4. Asset Management

- GUID-based asset identification system
- Custom serialization for specialized asset types
- External resource import pipeline

### 5. GameObject Framework

- Hierarchical object system with parent-child relationships
- Component-based architecture for game logic
- World management with controlled instantiation

## Documentation

- [Code Style Guide](./CODE_STYLE.md) - Coding conventions and best practices
- [Contributing Guide](./CONTRIBUTING.md) - How to contribute to this project
- [Technical Wiki](./wiki/) - Architecture and API documentation

## License

This project is licensed under the MIT License. See [LICENSE](./LICENSE) for details.
