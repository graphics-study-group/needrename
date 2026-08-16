# Design: Extract Physics DLL

## Context

The engine currently compiles four OBJECT libraries (`EngineLibFramework`, `EngineLibPhysics`, `EngineLibRender`, `EngineLibUserInterface`) plus `MainClass.cpp` and the imgui object files into a single `Engine.dll`. Three DLLs are already extracted: `EngineCore`, `EngineRhi`, `EngineAssetCore` — each with a per-DLL reflection target (`meta_core`, `meta_rhi`, `meta_asset_core`), an export header (`*_export.h`), and an `extern "C"` registration entry called from `MainClass.cpp`.

`Physics` is the most separation-ready module: all 11 headers are engine-dependency-free (AnnoRefl + glm only), no `.cpp` references `MainClass`, and SPIR-V is loaded from `ENGINE_PHYSICS_SPIRV_DIR` via `cmake_config.h` without the asset system. The single back-edge is `PhysicsScene.cpp:14` → `Framework/world/physics/PhysicsDescriptors.h`, which exists because the COM-space descriptors consumed by `PhysicsScene::SubmitRigidBody` / `SubmitCollisionShape` are defined on the Framework side.

Key data-flow facts that shape the design:

- Components (`RigidBodyComponent`, `CollisionShapeComponent`, `PhysicsConstraintComponent`) build GO-space descriptors and submit them to `PhysicsAdaptor`, which stores them as data snapshots in four pending maps (`m_pending_rigid_bodies`, `m_pending_shapes`, `m_pending_fixed_joints`, `m_pending_hinge_joints`).
- During `Flush`, the Adaptor resolves collision filters (ComponentHandle pairs → uint32 index pairs), computes COM/inertia, converts joints via `JointConverter`, and submits COM-space data to `PhysicsScene`.
- `PhysicsScene` already forward-declares the COM descriptors (`PhysicsScene.h:49-50`) and the Rhi types it uses; its headers only need AnnoRefl + glm.

## Goals / Non-Goals

**Goals:**
- Produce `EnginePhysics.dll` from the Physics module with explicit PUBLIC/PRIVATE dependencies and a `PHYSICS_API` export contract.
- Move the physics-interface descriptors into the Physics module: `RigidBodyComDescriptor`, `CollisionShapeComDescriptor`, plus `FixedJointComDescriptor` / `HingeJointComDescriptor` (renamed from `GpuFixedJoint` / `GpuHingeJoint`, std430 layout assertions preserved).
- Eliminate the `PhysicsScene.cpp` → Framework back-edge.
- Keep GO-space descriptors (`RigidBodyDescriptor`, `CollisionShapeDescriptor`, `FixedJointSubmitData`, `HingeJointSubmitData`) in Framework, redefined as Framework-internal transport structs.
- Add per-DLL reflection (`meta_physics` + `RegisterPhysicsTypes()`), remove `Physics/.*` from `meta_engine` scanning.
- Sync GLSL struct names in the two solver shaders.

**Non-Goals:**
- MainClass service-locator decoupling (the 27 module-internal `GetInstance()` calls) — a later change; Physics has zero such calls.
- Framework / Render / UserInterface extraction.
- Splitting the rest of `meta_engine` (Framework/Render/UI still share it).
- Migrating the SPIR-V path mechanism (`cmake_config.h` / `ENGINE_PHYSICS_SPIRV_DIR`) — kept as-is.
- Standalone physics-only test executables.

## Decisions

### D1: Descriptor classification — COM to Physics, GO stays in Framework

- **Decision**: The four COM-space descriptors move to a new `engine/Physics/PhysicsDescriptors.h`. The four GO-space descriptors remain in `Framework/world/physics/PhysicsDescriptors.h` (which keeps its `#include <Physics/PhysicsScene.h>` for `CollisionShapeType` / `INVALID_INDEX` — a legal direction), redefined as Framework-internal transport structs between components and the Adaptor.
- **Rationale**: The real physics interface is `PhysicsScene`; its inputs are the COM descriptors. GO descriptors are an internal Framework transport format — the Adaptor's pending maps are data snapshots (components submit during `Init`, `Flush` runs later; components may be destroyed in between), which a reference-based interface would invalidate.
- **Alternatives considered**: (a) move all six+ descriptors into Physics — rejected because `CollisionShapeDescriptor` carries `std::vector<ComponentHandle>` and would re-create a Physics → Framework dependency; (b) delete GO descriptors and let the Adaptor read component data itself — rejected: equivalent to moving `BuildDescriptor` assembly into the Adaptor with zero net gain, couples the Adaptor to component field layouts, and harms testability (lightweight POD snapshots are constructible without a Scene/GameObject stack).

### D2: Joint descriptor naming — unified COM family

- **Decision**: `GpuFixedJoint` → `FixedJointComDescriptor`, `GpuHingeJoint` → `HingeJointComDescriptor`. The GLSL struct names in `accumulate_fixed_position.comp` / `accumulate_hinge_position.comp` follow (shader-internal names, ABI unaffected; std430 size assertions 48/80 bytes preserved).
- **Rationale**: `PhysicsScene`'s four `Submit*` parameters then read as one coherent interface family (`RigidBodyComDescriptor`, `CollisionShapeComDescriptor`, `FixedJointComDescriptor`, `HingeJointComDescriptor`), and the "Com" suffix distinguishes the physics-interface layer from the GO-space `*SubmitData` types in Framework. Without the suffix, `FixedJointDescriptor` vs `FixedJointSubmitData` differ only in suffix and are easy to confuse.

### D3: Export contract — full `PHYSICS_API` coverage

- **Decision**: New `engine/Physics/physics_export.h` (same pattern as `core_export.h` / `asset_export.h`: `_WIN32` + `PHYSICS_DLL_EXPORTS` → `__declspec(dllexport/dllimport)`). Applied to classes `PhysicsScene`, `PhysicsSystem`, `ISolver`, `XpbdGpuSolver`, `DummySolver` (MainClass constructs `XpbdGpuSolver` in engine.dll and passes it across the DLL boundary via `RegisterSolver` — vtable must be exported), the four COM descriptors (constructed in Framework, consumed by `PhysicsScene`), and `PhysicsScene::PhysicsGpuBuffers` (read across the boundary by MainClass).
- **Rationale**: uniform, matches the Core/Rhi/AssetCore precedent, and future-proof for MSVC even though the current GNU toolchain auto-exports.

### D4: CMake structure — AssetCore pattern with narrowed deps

- `add_library(EnginePhysics SHARED ${SOURCE})` with `PHYSICS_DLL_EXPORTS` PRIVATE.
- **PUBLIC**: `EngineLibHeaderInterface` (include paths), `AnnoRefl` (reflection macros in headers), `EngineDepGlm` (glm in `PhysicsScene.h`).
- **PRIVATE**: `EngineRhi`, `EngineDepVulkan`, `EngineDepSdl` (all used only from `.cpp` files — `PhysicsScene.h` forward-declares Rhi types), `meta_physics`.
- `physics_shader` custom target stays in `engine/Physics/CMakeLists.txt`; `EnginePhysics` declares `add_dependencies(EnginePhysics physics_shader)`.
- `Engine` links `EnginePhysics` PUBLIC; `EngineLibFramework` links `EnginePhysics` PRIVATE (header visibility for its `#include <Physics/...>`); `meta_engine` filter gains `EXCLUDE REGEX "Physics/.*"`; POST_BUILD copies `$<TARGET_FILE:EnginePhysics>`; `$<TARGET_OBJECTS:EngineLibPhysics>`, its PRIVATE link, `add_dependencies(EngineLibPhysics meta_engine)`, and `add_dependencies(Engine physics_shader)` are removed (link ordering covers it).

### D5: Reflection wiring

- `meta_physics` via `filter_files_with_reflection_macros` (currently only `PhysicsScene.h` matches), no `parent_projects` (same as Core/Rhi/Asset).
- New `engine/Physics/PhysicsReflectionRegistration.cpp` mirrors `AssetReflectionRegistration.cpp`: `extern "C" PHYSICS_API void RegisterPhysicsTypes()`.
- `MainClass.cpp` adds the extern declaration and one call in the existing registration chain (after `RegisterAssetCoreTypes`, before `RegisterAllTypes`).
- `PhysicsScene.cpp:436`'s `#include "__generated__/PhysicsScene.h.inc"` is untouched — `meta_physics` regenerates the same wrapper path with content pointing at `meta_physics` outputs.

### D6: SPIR-V path stays put

- `ENGINE_PHYSICS_SPIRV_DIR` remains generated into the engine-root `cmake_config.h`; Physics `.cpp` files keep `#include <cmake_config.h>` (include dir provided by `EngineLibHeaderInterface`). Migrating to a Physics-owned config is deferred until Framework/Render extraction defines a shared pattern.

## Risks / Trade-offs

- [Missed `GpuFixedJoint` / `GpuHingeJoint` references during rename] → Full-repo grep already produced the complete list: `PhysicsScene.h` / `PhysicsScene.cpp`, `JointConverter.hpp`, `PhysicsAdaptor.cpp`, two GLSL shaders; openspec specs are updated in this change's delta specs.
- [Export-macro omission (MSVC scenario)] → all cross-DLL types get `PHYSICS_API` up front; GNU build remains the verification gate.
- [Reflection wrapper path mismatch] → `meta_physics` writes the same relative wrapper path (`Physics/__generated__/PhysicsScene.h.inc`); verified by inspecting AssetCore's identical mechanism.
- [Test executables can't find `EnginePhysics.dll` at runtime] → same mechanism as `EngineCore.dll` / `EngineAssetCore.dll` (POST_BUILD copy into the unified bin directory).
- [Shader rebuild churn] → GLSL rename triggers a `physics_shader` recompile once; no ABI change.

## Migration Plan

1. Create the OpenSpec change artifacts (this change), then implement tasks in dependency order: descriptor headers → renames → export macros → CMake targets → reflection registration → engine wiring.
2. Verify: `cmake --build --preset debug` green, `ctest --preset debug` green, `physics_example` runs on GPU, `EnginePhysics.dll` present in the output directory, reflection registration chain active (physical tests pass).
3. Rollback: revert the single implementation commit; the change is additive (new DLL + renames) with no persistent data migration.
