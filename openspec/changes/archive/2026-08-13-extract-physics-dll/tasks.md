## 1. Physics interface descriptors (engine/Physics/PhysicsDescriptors.h)

- [x] 1.1 Create `engine/Physics/PhysicsDescriptors.h` defining `RigidBodyComDescriptor` and `CollisionShapeComDescriptor` (moved from Framework), plus `FixedJointComDescriptor` and `HingeJointComDescriptor` (renamed from `GpuFixedJoint` / `GpuHingeJoint`), preserving the std430 size assertions (`sizeof(FixedJointComDescriptor) == 48`, `sizeof(HingeJointComDescriptor) == 80`)
- [x] 1.2 Slim `Framework/world/physics/PhysicsDescriptors.h` down to the GO-space transport structs (`RigidBodyDescriptor`, `CollisionShapeDescriptor`, `FixedJointSubmitData`, `HingeJointSubmitData`), redefining them as Framework-internal transport structs in the header comment; keep `#include <Physics/PhysicsScene.h>`
- [x] 1.3 Update `PhysicsScene.h`: remove the `GpuFixedJoint` / `GpuHingeJoint` struct definitions, include `"PhysicsDescriptors.h"`, rename the `SubmitFixedJoint` / `SubmitHingeJoint` parameter types and the `m_fixed_joints` / `m_hinge_joints` member types
- [x] 1.4 Update `PhysicsScene.cpp`: replace the `#include <Framework/world/physics/PhysicsDescriptors.h>` back-edge with the module-local header, rename all `GpuFixedJoint` / `GpuHingeJoint` references
- [x] 1.5 Update `Framework/world/physics/Internal/JointConverter.hpp`: `ConvertFixed` / `ConvertHinge` return types become `FixedJointComDescriptor` / `HingeJointComDescriptor`
- [x] 1.6 Update `Framework/world/physics/PhysicsAdaptor.cpp`: local `GpuFixedJoint` / `GpuHingeJoint` variables use the renamed descriptor types

## 2. Export contract

- [x] 2.1 Create `engine/Physics/physics_export.h` defining `PHYSICS_API` (same pattern as `core_export.h` / `asset_export.h`, keyed on `PHYSICS_DLL_EXPORTS`)
- [x] 2.2 Annotate `PhysicsScene`, `PhysicsSystem`, `ISolver`, `XpbdGpuSolver`, `DummySolver`, the four COM descriptors, and `PhysicsScene::PhysicsGpuBuffers` with `PHYSICS_API`

## 3. GLSL struct renames

- [x] 3.1 Rename the struct in `engine/Physics/shader/solver/XPBDSolver/accumulate_fixed_position.comp` to `FixedJointComDescriptor`
- [x] 3.2 Rename the struct in `engine/Physics/shader/solver/XPBDSolver/accumulate_hinge_position.comp` to `HingeJointComDescriptor`

## 4. CMake restructuring

- [x] 4.1 Rework `engine/Physics/CMakeLists.txt`: `EngineLibPhysics` OBJECT becomes `EnginePhysics` SHARED with `PHYSICS_DLL_EXPORTS`; PUBLIC deps `EngineLibHeaderInterface` / `AnnoRefl` / `EngineDepGlm`; PRIVATE deps `EngineRhi` / `EngineDepVulkan` / `EngineDepSdl` / `meta_physics`; keep `add_dependencies(EnginePhysics physics_shader)`
- [x] 4.2 Update `engine/CMakeLists.txt`: remove `$<TARGET_OBJECTS:EngineLibPhysics>` and its PRIVATE link, link `EnginePhysics` PUBLIC; exclude `Physics/.*` from `meta_engine` scanning; add `$<TARGET_FILE:EnginePhysics>` to the POST_BUILD copy; remove `add_dependencies(EngineLibPhysics meta_engine)` and `add_dependencies(Engine physics_shader)`
- [x] 4.3 Update `engine/Framework/CMakeLists.txt`: `EngineLibFramework` links `EnginePhysics` PRIVATE for header visibility

## 5. Reflection registration

- [x] 5.1 Create `engine/Physics/PhysicsReflectionRegistration.cpp` exposing `extern "C" PHYSICS_API void RegisterPhysicsTypes()` (mirror `AssetReflectionRegistration.cpp`)
- [x] 5.2 Add the extern declaration and the `RegisterPhysicsTypes()` call to the registration chain in `MainClass.cpp` (after `RegisterAssetCoreTypes`, before `RegisterAllTypes`)

## 6. Verification

- [x] 6.1 `cmake --build --preset debug` completes green
- [x] 6.2 `ctest --preset debug` passes with the same test count as before
- [x] 6.3 `EnginePhysics.dll` exists in the output directory and was copied by the engine POST_BUILD step
- [x] 6.4 `physics_example` runs on GPU with physics simulation working (reflection registration chain active)
