## 1. Enum and Type Definitions

- [x] 1.1 Add `Sphere = 1` and `Cylinder = 2` to `CollisionShapeType` enum in [PhysicsScene.h](engine/Physics/PhysicsScene.h)
- [x] 1.2 Add `#define SHAPE_TYPE_SPHERE 1u` and `#define SHAPE_TYPE_CYLINDER 2u` GLSL constants in [support.glsl](engine/Physics/shader/solver/ConvexCollisionDetector/support.glsl)

## 2. CollisionShapeComponent Field Rename and Type Handling

- [x] 2.1 Rename `m_box_size` → `m_feature`, `m_box_center` → `m_center`, `m_box_rotation` → `m_rotation` in [CollisionShapeComponent.h](engine/Framework/component/physics/CollisionShapeComponent.h)
- [x] 2.2 Change `m_feature` default from `{1.0, 1.0, 1.0}` (full size) to `{0.5, 0.5, 0.5}` (half-extents) and update doc comments
- [x] 2.3 Remove `* 0.5f` conversion in `Awake()` — pass `m_feature` directly to `RegisterCollisionShape`/`UpdateCollisionShapeGeometry` (Box now stores half-extents natively)
- [x] 2.4 Implement non-uniform XY scale detection for Cylinder in `Awake()`: extract world scale, check `|sx - sy| > 1e-4f`, fall back to Box with bounding half-extents, emit `SDL_LogWarn`
- [x] 2.5 Update `GetLocalCenterInParentSpace()` to return `m_center`, `GetLocalRotationInParentSpace()` to return `m_rotation`
- [x] 2.6 Run reflection parser to regenerate `__generated__/CollisionShapeComponent.h.inc` serialization code

## 3. PhysicsScene Feature Rename

- [x] 3.1 Rename `m_shape_half_extents` → `m_shape_feature` in PhysicsScene.h private members
- [x] 3.2 Rename `shape_half_extents` → `shape_feature` in `PhysicsGpuBuffers` struct and `m_gpu_shape_half_extents` → `m_gpu_shape_feature`
- [x] 3.3 Rename `half_extents` parameter to `feature` in `RegisterCollisionShape()`, `UpdateCollisionShapeGeometry()` signatures and doc comments
- [x] 3.4 Update all `m_shape_half_extents` references in [PhysicsScene.cpp](engine/Physics/PhysicsScene.cpp) (Clear, Register, Update, GetGpuBuffers, Recalculate, RefreshGpuBuffers, DebugPrint) — ~20 occurrences

## 4. Shape Inertia and Volume Functions

- [x] 4.1 Implement `ComputeBoxVolume()`, `ComputeSphereVolume()`, `ComputeCylinderVolume()` static functions in PhysicsScene.cpp
- [x] 4.2 Implement `ComputeBoxInertia()`, `ComputeSphereInertia()`, `ComputeCylinderInertia()` static functions in PhysicsScene.cpp (cylinder: Z-up formulas)
- [x] 4.3 Refactor `RecalculateRigidBodyState()` to dispatch volume and inertia by `m_shape_type[shape_index]` using switch (Box/Sphere/Cylinder)
- [x] 4.4 Add `SDL_LogWarn` for unknown shape types in the inertia dispatch, contributing zero volume/inertia

## 5. GPU Buffer and Collision Detector Sync

- [x] 5.1 Rename `ShapeHalfExtents` → `ShapeFeature` buffer binding name in [ConvexCollisionDetector.cpp](engine/Physics/Collision/ConvexCollisionDetector.cpp) (`BindBuffer` call and render graph handle variable)
- [x] 5.2 Rename buffer declaration in [detect_collisions.comp](engine/Physics/shader/solver/ConvexCollisionDetector/detect_collisions.comp): `ShapeHalfExtents` → `ShapeFeature`, `shape_half_extents` → `shape_feature`

## 6. GLSL Support Functions

- [x] 6.1 Implement `support_sphere(feature, world_pos, world_rot, dir_world)` in [support.glsl](engine/Physics/shader/solver/ConvexCollisionDetector/support.glsl): `world_pos + normalize(dir) * feature.x`
- [x] 6.2 Implement `support_cylinder(feature, world_pos, world_rot, dir_world)` in support.glsl: Z-up decomposition (Z axial + XY radial)
- [x] 6.3 Extend `support()` dispatch to call `support_sphere` for type 1u and `support_cylinder` for type 2u
- [x] 6.4 Update all `shape_half_extents` references to `shape_feature` in support.glsl

## 7. SceneBuilder Extensions

- [x] 7.1 Add `static_friction`, `dynamic_friction`, `restitution` fields to `BoxDesc` in [SceneBuilder.h](example/physics_example/SceneBuilder.h)
- [x] 7.2 Add `SphereDesc` struct with `position`, `rotation`, `radius`, `mass`, `kinematic`, `static_friction`, `dynamic_friction`, `restitution`, `material`
- [x] 7.3 Add `CylinderDesc` struct with `position`, `rotation`, `radius`, `half_height`, `mass`, `kinematic`, `static_friction`, `dynamic_friction`, `restitution`, `material`
- [x] 7.4 Add `AddSphere(const SphereDesc&)` and `AddCylinder(const CylinderDesc&)` method declarations
- [x] 7.5 Add `m_sphere_mesh` and `m_cylinder_mesh` private members; load `~/mesh/sphere.asset` and `~/mesh/cylinder.asset` in constructor
- [x] 7.6 Update `AddBox` in [SceneBuilder.cpp](example/physics_example/SceneBuilder.cpp): use `m_feature`/`m_center`/`m_rotation`, set `m_feature = desc.half_extents` (remove `* 2.0f` — component now stores half-extents directly), set friction/restitution on RigidBodyComponent
- [x] 7.7 Implement `AddSphere` in [SceneBuilder.cpp](example/physics_example/SceneBuilder.cpp): parent GO with RigidBodyComponent, Mesh child with sphere mesh scaled by `vec3(radius)`, Collision child with `m_shape_type = Sphere`, `m_feature = {radius, 0, 0}`
- [x] 7.8 Implement `AddCylinder` in SceneBuilder.cpp: parent GO with RigidBodyComponent, Mesh child with cylinder mesh scaled by `vec3(radius, radius, half_height)`, Collision child with `m_shape_type = Cylinder`, `m_feature = {radius, half_height, 0}`

## 8. Test and Example Updates

- [x] 8.1 Update `m_box_size` → `m_feature` in [physics_registration_test.cpp](test/physics_registration_test.cpp); halve all values: `{2,2,2}` → `{1,1,1}`, `{1,1,1}` → `{0.5,0.5,0.5}` (×5 occurrences)
- [x] 8.2 Update `m_box_center` → `m_center` references in physics_registration_test.cpp
- [x] 8.3 Add test cases for sphere and cylinder volume/inertia functions (verify known values)
- [x] 8.4 Verify physics example compiles and runs with mixed Box/Sphere/Cylinder shapes

## 9. Build and Verification

- [x] 9.1 Run CMake reconfigure and build — verify `physics_shader` target regenerates `detect_collisions.comp.spv` with updated `support.glsl`
- [x] 9.2 Run existing physics tests (`ctest`) — verify no regressions
- [x] 9.3 Run the physics example and visually verify sphere and cylinder rendering with correct collision bounds
