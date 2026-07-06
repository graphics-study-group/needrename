# sphere-collision-shape

## Purpose

Define sphere primitive collision shape support: GPU support function, CPU-side volume and inertia computation, component integration, and SceneBuilder mesh matching.

## ADDED Requirements

### Requirement: Sphere support function in GLSL

The collision detection shader SHALL provide a `support_sphere(feature, world_pos, world_rot, dir_world)` function that returns the world-space farthest point on a sphere in the given direction. The function SHALL compute `world_pos + normalize(dir_world) * feature.x` where `feature.x` is the sphere radius. The `world_rot` parameter SHALL be accepted but ignored (sphere is rotationally invariant). When the direction vector has near-zero length (< 1e-8), the function SHALL return `world_pos`.

#### Scenario: Sphere support along +X axis
- **WHEN** `support_sphere(vec3(2.0, 0, 0), vec3(0,0,0), identity_quat, vec3(1,0,0))` is called
- **THEN** the returned point is `(2.0, 0, 0)`

#### Scenario: Sphere support along diagonal
- **WHEN** `support_sphere` is called with radius 1.0 at origin and direction `(1, 1, 1)`
- **THEN** the returned point is `normalize(1,1,1) * 1.0` = `(0.577..., 0.577..., 0.577...)`

#### Scenario: Sphere support with near-zero direction
- **WHEN** `support_sphere` is called with a zero-length direction vector
- **THEN** the function returns `world_pos` unchanged

### Requirement: Sphere volume computation

The system SHALL compute sphere volume as `(4.0/3.0) * π * r³` where `r = feature.x`. The computation SHALL be in a dedicated `ComputeSphereVolume(float radius)` function in `PhysicsScene.cpp`.

#### Scenario: Unit sphere volume
- **WHEN** `ComputeSphereVolume(1.0f)` is called
- **THEN** the returned value is approximately `4.18879` (4π/3)

#### Scenario: Zero radius sphere
- **WHEN** `ComputeSphereVolume(0.0f)` is called
- **THEN** the returned value is `0.0f`

### Requirement: Sphere inertia tensor

The system SHALL compute the solid sphere inertia tensor as `(2/5) * m * r² * I` (diagonal matrix with equal entries). The computation SHALL be in a dedicated `ComputeSphereInertia(float mass, float radius)` function returning `glm::mat3`, in `PhysicsScene.cpp`.

#### Scenario: Unit sphere inertia
- **WHEN** `ComputeSphereInertia(1.0f, 1.0f)` is called
- **THEN** the returned matrix is `diag(0.4, 0.4, 0.4)` — all diagonal entries = 2/5 = 0.4

#### Scenario: Scaled sphere inertia
- **WHEN** `ComputeSphereInertia(2.0f, 1.0f)` is called (mass=2, r=1)
- **THEN** the returned matrix is `diag(0.8, 0.8, 0.8)` — all diagonal entries = 0.8

### Requirement: Sphere collision shape type constant

The `CollisionShapeType` enum SHALL include `Sphere = 1`. The GLSL `support()` dispatch SHALL use `#define SHAPE_TYPE_SPHERE 1u` and call `support_sphere()` when `shape_type == SHAPE_TYPE_SPHERE`.

#### Scenario: Sphere type registered in physics scene
- **WHEN** a `CollisionShapeComponent` has `m_shape_type = CollisionShapeType::Sphere` and `m_feature = {1.5, 0, 0}`
- **THEN** `PhysicsScene::RegisterCollisionShape` stores type=1 and feature=(1.5, 0, 0)
- **AND** `RecalculateRigidBodyState` uses sphere volume and inertia formulas

### Requirement: Sphere mesh matching in SceneBuilder

`SceneBuilder::AddSphere(const SphereDesc&)` SHALL load the builtin `~/mesh/sphere.asset` mesh and set the mesh child's transform scale to `vec3(desc.radius)` so the visual mesh (radius 1m) matches the collision radius.

#### Scenario: Default sphere scale
- **WHEN** `AddSphere` is called with `radius = 0.5f`
- **THEN** the mesh child's transform scale is `(0.5, 0.5, 0.5)`

#### Scenario: Sphere with different radius
- **WHEN** `AddSphere` is called with `radius = 2.0f`
- **THEN** the mesh child's transform scale is `(2.0, 2.0, 2.0)`
