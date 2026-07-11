> 🇨🇳 [中文版](./docs/README_CN.md) | 🇺🇸 English

An unnamed game engine with GPU-accelerated physics simulation, Vulkan-based rendering, Python-powered reflection/serialization, and a flexible component-based game framework.

![engine_editor](./assets/img/engine_editor.png)

## Building the Engine

### Dependencies

- GCC 14 or greater
- CMake
- Python 3
- Vulkan SDK 1.3 or greater (Tested on 1.4.313)
- SDL3 (Tested on 3.2.18)

Other vendored dependencies can be found in the `third_party` directory.

When working on Windows, use of MSYS2 is suggested. You can set up the environment with

```sh
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake
```

which installs GCC and CMake for the UCRT64 subsystem.

Vulkan SDK should be downloaded and installed from LunarG, but *not* from MSYS2 repo with `pacman`, which misses some components and is difficult to integrate with CMake.

It is suggested that SDL3 should also be installed manually.
You can fetch it from [its release page](https://github.com/libsdl-org/SDL/releases/).
Pick `SDL3-devel-3.X.XX-mingw.tar.gz`, extract it somewhere, and:

- Add a new environment variable `SDL3_DIR` pointing to `SDL3-3.X.XX\cmake`.
- Add a `PATH` entry pointing to `SDL3-3.X.XX\x86_64-w64-mingw32\bin`.

CMake should be able to detect it automatically.

### Build Steps

1. `git clone` this repository (with `--recursive` flag)
2. Configure and build project (with Vulkan SDK installed) using cmake. Out-of-source build is preferred:

```sh
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -G "MinGW Makefiles"
mingw32-make
```

Or you can use your favorite IDE to do so.
If you are using Visual Studio Code on Windows, after updating environment variables, the integrated terminal and CMake might not use updated values even after restarting VSCode.
Ensure that you have killed all background VSCode processes before restarting, or simply [re-logging or restart your computer](https://github.com/microsoft/vscode/issues/69289#issuecomment-467595799) to solve this issue.

## Project Structure

```
docs/                     # Contribution guidelines and code style
wiki/                    # Technical documentation
assets/                  # Raw resources
builtin_assets/          # Built-in assets used across all projects
editor/                  # Engine editor code
engine/
    Asset/               # Asset management
    Core/                # Core features (Math, Functional)
    Framework/           # GameObject, Component, Scene
    Physics/             # GPU-accelerated physics engine
    Reflection/          # Reflection and serialization
    Render/              # Vulkan rendering systems
    UserInterface/       # GUI system
example/                 # Runnable game examples
    physics_example/     # Physics simulation demo
projects/                # Example game projects
reflection_parser/       # Python parser for C++ reflection
test/                    # Test executables
third_party/             # External dependencies (glm, SPIRV-Cross)
```

## Build Targets

- **editor**: Executable that runs the engine editor interface
- **engine**: Static library containing core engine functionality
- **tests**: Executable demos and test cases (runnable via CTest)
- **third_party**: Static libraries for dependencies

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
