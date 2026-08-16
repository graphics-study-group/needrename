# physics-dll-module

## Purpose

Define the Physics module as a standalone shared library `EnginePhysics.dll`: explicit PUBLIC/PRIVATE CMake dependencies, the `PHYSICS_API` export contract, physics-interface descriptors owned by the module, per-DLL reflection registration, and shader build ownership. This makes the Physics module the first consumer-facing module extracted from `engine.dll` (after the leaf DLLs Core / Rhi / AssetCore).

## Requirements

### Requirement: Physics module builds as EnginePhysics shared library

The Physics module SHALL build as a shared library target named `EnginePhysics` producing `EnginePhysics.dll`. The target SHALL be compiled from all sources under `engine/Physics/` and SHALL define `PHYSICS_DLL_EXPORTS` privately.

The target SHALL declare the following dependencies:
- PUBLIC: `EngineLibHeaderInterface`, `AnnoRefl`, `EngineDepGlm` (header-facing: include paths, reflection macros, glm in `PhysicsScene.h`)
- PRIVATE: `EngineRhi`, `EngineDepVulkan`, `EngineDepSdl` (used only from `.cpp` files; `PhysicsScene.h` forward-declares Rhi types), and `meta_physics`

The main `Engine` shared library SHALL link `EnginePhysics` PUBLIC, and the `EngineLibFramework` OBJECT library SHALL link `EnginePhysics` PRIVATE so Framework translation units can include `<Physics/...>` headers. The engine post-build copy step SHALL copy `EnginePhysics.dll` into the unified output directory.

The `EngineLibPhysics` OBJECT library target SHALL NOT exist.

#### Scenario: EnginePhysics target exists
- **WHEN** the CMake configuration is generated
- **THEN** a target named `EnginePhysics` exists and is a SHARED library
- **AND** no target named `EngineLibPhysics` exists

#### Scenario: Build produces EnginePhysics.dll
- **WHEN** `cmake --build --preset debug` completes
- **THEN** `EnginePhysics.dll` exists in the build output directory
- **AND** the DLL was copied next to `Engine.dll` by the engine post-build step

#### Scenario: Framework sees Physics headers
- **WHEN** `EngineLibFramework` translation units include `<Physics/PhysicsScene.h>`
- **THEN** the header is found and the module compiles

### Requirement: Physics interface descriptors are owned by the Physics module

The COM-space physics-interface descriptors SHALL be defined in `engine/Physics/PhysicsDescriptors.h`:
- `RigidBodyComDescriptor`
- `CollisionShapeComDescriptor`
- `FixedJointComDescriptor` (renamed from `GpuFixedJoint`)
- `HingeJointComDescriptor` (renamed from `GpuHingeJoint`)

The structs SHALL carry `PHYSICS_API`. The std430 size assertions SHALL be preserved: `sizeof(FixedJointComDescriptor) == 48` and `sizeof(HingeJointComDescriptor) == 80`.

`PhysicsScene.h` SHALL remain engine-dependency-free (AnnoRefl + glm only); it SHALL forward-declare or include the descriptors within the module without introducing any Framework include.

#### Scenario: Descriptor structs located in Physics module
- **WHEN** the source tree is inspected
- **THEN** `engine/Physics/PhysicsDescriptors.h` exists
- **AND** it defines the four COM descriptors
- **AND** `engine/Framework/` contains none of them

#### Scenario: Size assertions preserved
- **WHEN** the C++ types are compiled
- **THEN** `sizeof(FixedJointComDescriptor) == 48` and `sizeof(HingeJointComDescriptor) == 80` hold

#### Scenario: No Framework include in Physics headers
- **WHEN** every header under `engine/Physics/` is inspected
- **THEN** none of them includes a `Framework/`, `Render/`, `Asset/`, or `UserInterface/` header

### Requirement: Physics classes export PHYSICS_API

The classes `PhysicsScene`, `PhysicsSystem`, `ISolver`, `XpbdGpuSolver`, `DummySolver`, and the struct `PhysicsScene::PhysicsGpuBuffers` SHALL be declared with `PHYSICS_API` (defined in `engine/Physics/physics_export.h`, pattern of `core_export.h` / `asset_export.h`).

#### Scenario: Cross-DLL instantiation works
- **WHEN** `MainClass` (in `Engine.dll`) constructs an `XpbdGpuSolver` and registers it via `PhysicsSystem::RegisterSolver`
- **THEN** the type symbols resolve across the DLL boundary
- **AND** `PhysicsScene::PhysicsGpuBuffers` fields are readable from `Engine.dll`

### Requirement: Physics reflection is registered per-DLL

A `meta_physics` reflection target SHALL scan the Physics module headers containing reflection macros and generate code into `engine/Physics/__generated__/`. A `PhysicsReflectionRegistration.cpp` translation unit SHALL expose `extern "C" PHYSICS_API void RegisterPhysicsTypes()` that registers all Physics reflection types.

The engine registration chain in `MainClass.cpp` SHALL call `RegisterPhysicsTypes()`.

The `meta_engine` reflection target SHALL NOT scan files under `engine/Physics/`.

#### Scenario: Reflection types registered at startup
- **WHEN** `MainClass` initializes the reflection system
- **THEN** `RegisterPhysicsTypes()` is invoked
- **AND** the `CollisionShapeType` enum is registered with the reflection registry

#### Scenario: meta_engine excludes Physics
- **WHEN** the `meta_engine` reflection configuration is generated
- **THEN** no input file under `engine/Physics/` is scanned

### Requirement: physics_shader build dependency belongs to EnginePhysics

The `physics_shader` CMake target SHALL be declared and built under `engine/Physics/CMakeLists.txt`. The `EnginePhysics` target SHALL declare a build dependency on `physics_shader`. The engine SHALL obtain physics SPIR-V artefacts transitively through its link dependency on `EnginePhysics`.

#### Scenario: Engine build produces physics SPIR-V
- **WHEN** the developer builds the `Engine` target
- **THEN** all physics SPIR-V artefacts under `${CMAKE_BINARY_DIR}/engine/Physics/spirv/` are produced
- **AND** `physics_shader` builds before `EnginePhysics` links

### Requirement: Physics headers keep zero engine dependency

Every header under `engine/Physics/` SHALL continue to depend only on AnnoRefl, glm, and the C++ standard library. In particular, `PhysicsScene.h` SHALL NOT include any Framework header, and the back-edge `PhysicsScene.cpp → Framework/world/physics/PhysicsDescriptors.h` SHALL be eliminated (the cpp SHALL consume `engine/Physics/PhysicsDescriptors.h` instead).

#### Scenario: No framework dependency in physics headers
- **WHEN** all `engine/Physics/**/*.h` files are inspected
- **THEN** none includes `Framework/` headers

#### Scenario: PhysicsScene.cpp has no Framework include
- **WHEN** `engine/Physics/PhysicsScene.cpp` is inspected
- **THEN** its descriptor include points into `engine/Physics/`, not `engine/Framework/`
