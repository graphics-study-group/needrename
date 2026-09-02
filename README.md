> 🇨🇳 [中文版](./docs/README_CN.md) | 🇺🇸 English

An unnamed game engine with GPU-accelerated physics simulation, Vulkan-based rendering, Python-powered reflection/serialization, and a flexible component-based game framework.

![engine_editor](./assets/img/engine_editor.png)

## Building the Engine

The engine builds on two platforms:

- **Windows** — MSYS2 CLANG64 toolchain (Clang + Ninja). Full setup and build instructions: [`docs/build_instructions/windows_msys2_clang64.md`](./docs/build_instructions/windows_msys2_clang64.md)
- **Linux** — Clang + Ninja with a manually installed LunarG Vulkan SDK and SDL3. Full setup and build instructions: [`docs/build_instructions/linux.md`](./docs/build_instructions/linux.md)

Shared CMake presets (`debug` / `release`) are defined in `CMakePresets.json`; Linux adds `linux-debug` / `linux-release`. Platform-specific configuration (environment variables, toolchains, interpreters) lives in the per-platform docs above.

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

- [Build Instructions](./docs/build_instructions/) - Platform-specific build setup (Windows MSYS2 CLANG64 / Linux)
- [Code Style Guide](./CODE_STYLE.md) - Coding conventions and best practices
- [Contributing Guide](./CONTRIBUTING.md) - How to contribute to this project
- [Technical Wiki](./wiki/) - Architecture and API documentation

## License

This project is licensed under the MIT License. See [LICENSE](./LICENSE) for details.
