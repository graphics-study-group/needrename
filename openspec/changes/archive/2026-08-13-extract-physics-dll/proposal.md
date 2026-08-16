# Proposal: Extract Physics DLL

## Why

`Physics` is the cleanest of the four remaining OBJECT libraries in `engine.dll`: all its headers are engine-dependency-free, it has zero `MainClass::GetInstance()` calls, and it loads SPIR-V via `cmake_config.h` without touching the asset system. Its only obstacle is a single back-edge — `PhysicsScene.cpp:14` includes `Framework/world/physics/PhysicsDescriptors.h` because the COM-space descriptors it consumes are defined on the Framework side. Now that Core / Rhi / AssetCore DLLs are established, extracting Physics as the first consumer-facing module DLL validates the full CMake/reflection/shader pattern while removing that last back-edge.

## What Changes

- **New `EnginePhysics` shared library**: `EngineLibPhysics` OBJECT library becomes a standalone DLL (`EnginePhysics.dll`), following the AssetCore CMake pattern (`PHYSICS_DLL_EXPORTS`, per-DLL reflection, `physics_shader` build dependency). **BREAKING**: the `EngineLibPhysics` CMake target no longer exists.
- **Physics interface descriptors move into the Physics module**: new `engine/Physics/PhysicsDescriptors.h` defines `RigidBodyComDescriptor`, `CollisionShapeComDescriptor`, `FixedJointComDescriptor` (renamed from `GpuFixedJoint`), and `HingeJointComDescriptor` (renamed from `GpuHingeJoint`), with std430 size assertions preserved. **BREAKING**: `GpuFixedJoint` / `GpuHingeJoint` are renamed.
- **GO-space descriptors stay in Framework**: `Framework/world/physics/PhysicsDescriptors.h` keeps only `RigidBodyDescriptor`, `CollisionShapeDescriptor`, `FixedJointSubmitData`, `HingeJointSubmitData`, redefined as Framework-internal transport structs (component → Adaptor). The back-edge `PhysicsScene.cpp → Framework` is eliminated.
- **Export contract**: new `engine/Physics/physics_export.h` (`PHYSICS_API`); exported classes `PhysicsScene`, `PhysicsSystem`, `ISolver`, `XpbdGpuSolver`, `DummySolver`, the four COM descriptors, and `PhysicsScene::PhysicsGpuBuffers`.
- **Per-DLL reflection**: new `meta_physics` target scans Physics headers; `PhysicsReflectionRegistration.cpp` exposes `RegisterPhysicsTypes()`; `MainClass` registration chain gains one call. `meta_engine` no longer scans `Physics/.*`.
- **GLSL struct renames**: `GpuFixedJoint` / `GpuHingeJoint` struct names in `accumulate_fixed_position.comp` / `accumulate_hinge_position.comp` follow the C++ rename (layout and ABI unchanged).
- **Dependency wiring**: `Engine` links `EnginePhysics` PUBLIC; `EngineLibFramework` links it PRIVATE for header visibility; POST_BUILD copies `EnginePhysics.dll`; engine depends on `physics_shader` only transitively through `EnginePhysics`.

## Capabilities

### New Capabilities
- `physics-dll-module`: the Physics module builds as a standalone `EnginePhysics.dll` with explicit PUBLIC/PRIVATE dependencies, export macros, per-DLL reflection registration, and shader build ownership.

### Modified Capabilities
- `module-target-naming`: `EngineLibPhysics` leaves the unchanged OBJECT-library family; `EnginePhysics` joins the `Engine`-prefixed shared-library family and is copied post-build.
- `physics-gpu-shaders`: the `physics_shader` build dependency is declared by `EnginePhysics` (and the engine target), not by `EngineLibPhysics`.
- `com-descriptors`: `SubmitFixedJoint` / `SubmitHingeJoint` parameter types become `FixedJointComDescriptor` / `HingeJointComDescriptor`; COM descriptors are defined in `engine/Physics/PhysicsDescriptors.h`.
- `go-descriptors`: GO-space descriptors are located in `Framework/world/physics/PhysicsDescriptors.h` and are Framework-internal transport structs, not physics-interface types.
- `physics-adaptor-flush`: `JointConverter` outputs the renamed `FixedJointComDescriptor` / `HingeJointComDescriptor` types.
- `hinge-joint-constraint`: the 80-byte std430 struct is renamed `HingeJointComDescriptor` (layout requirements unchanged).
- `rigidbody-center-of-mass-offset`: references to `GpuFixedJoint` / `GpuHingeJoint` are updated to the renamed descriptor types.

## Impact

- **Code**: `engine/Physics/` (all files — export macros, descriptor header, reflection registration, CMake), `engine/Framework/world/physics/` (`PhysicsDescriptors.h` slims to GO-space, `JointConverter.hpp` / `PhysicsAdaptor.cpp` type renames), `engine/MainClass.cpp` (registration chain), `engine/CMakeLists.txt` + `engine/Framework/CMakeLists.txt` (target wiring).
- **GLSL**: two solver shaders (struct name only).
- **Build artifacts**: `EnginePhysics.dll` produced and copied to the output directory; `EngineLibPhysics` target removed.
- **No runtime behavior change**: layout assertions (48/80 bytes) and the GPU buffer ABI are preserved.
