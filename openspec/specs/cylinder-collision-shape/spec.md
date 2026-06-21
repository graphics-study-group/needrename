# cylinder-collision-shape

## Purpose

Define Z-up cylinder primitive collision shape support: GPU support function (axial Z + radial XY decomposition), CPU-side volume and inertia computation, non-uniform scale detection with box fallback, component integration, and SceneBuilder mesh matching.

## Requirements

### Requirement: Cylinder support function in GLSL

The collision detection shader SHALL provide a `support_cylinder(feature, world_pos, world_rot, dir_world)` function that returns the world-space farthest point on a Z-up cylinder. The function SHALL transform `dir_world` to local space via `quat_inv_rotate`, decompose into Z (axial) and XY (radial) components, compute `z_sign * feature.y` for the axial contribution and `normalize(dir_xy) * feature.x` for the radial contribution, transform the result back to world space, and add `world_pos`. When the XY direction component has near-zero length (< 1e-8), the radial contribution SHALL be `(0, 0)`.

#### Scenario: Cylinder support along +Z axis (top face)
- **WHEN** `support_cylinder` is called with `feature = (1.0, 0.5, 0)` (r=1, half_h=0.5), identity rotation, at origin, and direction `(0, 0, 1)`
- **THEN** the returned point is `(0, 0, 0.5)` — the center of the top face

#### Scenario: Cylinder support along +X axis (side)
- **WHEN** `support_cylinder` is called with `feature = (1.0, 0.5, 0)`, identity rotation, at origin, and direction `(1, 0, 0)`
- **THEN** the returned point is `(1.0, 0, 0)` — the point on the side at Z=0

#### Scenario: Cylinder support along diagonal
- **WHEN** `support_cylinder` is called with `feature = (1.0, 0.5, 0)`, identity rotation, at origin, and direction `(0.6, 0, 0.8)` (mostly Z with some X)
- **THEN** the returned point has `z = 0.5` (top face), `x = 1.0` (right side), `y = 0`

#### Scenario: Cylinder support with rotated cylinder
- **WHEN** `support_cylinder` is called with a cylinder rotated 90° around X axis
- **THEN** the direction is inversely rotated to the cylinder's local space before decomposition
- **AND** the support point is rotated back to world space

### Requirement: Cylinder volume computation

The system SHALL compute cylinder volume as `π * r² * h` where `r = feature.x` and `h = 2 * feature.y` (total height = 2 × half_height). The computation SHALL be in a dedicated `ComputeCylinderVolume(float radius, float half_height)` function in `PhysicsScene.cpp`.

#### Scenario: Unit cylinder volume
- **WHEN** `ComputeCylinderVolume(1.0f, 1.0f)` is called (r=1, half_h=1 → h=2)
- **THEN** the returned value is approximately `6.28318` (2π)

#### Scenario: Zero-size cylinder
- **WHEN** `ComputeCylinderVolume(0.0f, 0.0f)` or `ComputeCylinderVolume(1.0f, 0.0f)` is called
- **THEN** the returned value is `0.0f`

### Requirement: Cylinder inertia tensor (Z-up)

The system SHALL compute the solid cylinder inertia tensor with Z as the height axis:
- I_zz (axial, about Z) = `(1/2) * m * r²`
- I_xx = I_yy (transverse) = `(1/12) * m * (3r² + h²)` where `h = 2 * half_height`
- Off-diagonal entries SHALL be zero.

The computation SHALL be in a dedicated `ComputeCylinderInertia(float mass, float radius, float half_height)` function returning `glm::mat3`, in `PhysicsScene.cpp`.

#### Scenario: Unit cylinder inertia (r=1, half_h=1, h=2)
- **WHEN** `ComputeCylinderInertia(1.0f, 1.0f, 1.0f)` is called
- **THEN** I_zz = 0.5, I_xx = I_yy = `(1/12)*(3+4)` = `7/12 ≈ 0.5833`

#### Scenario: Flat disk inertia (r=1, half_h→0, h→0)
- **WHEN** `ComputeCylinderInertia(1.0f, 1.0f, 0.001f)` is called
- **THEN** I_zz ≈ 0.5, I_xx = I_yy ≈ `(1/12)*(3)` = `0.25` (approaches disk inertia)

### Requirement: Non-uniform cylinder scale detection and fallback

When a `CollisionShapeComponent` has `m_shape_type == Cylinder`, `Awake()` SHALL extract the world transform scale from the owning GameObject. If `|scale.x - scale.y| > 1e-4f`, the system SHALL:
1. Emit `SDL_LogWarn` with the GameObject identifier, scale values, and a message indicating the cylinder is falling back to a bounding box
2. Compute bounding box half-extents as `(r * |scale.x|, r * |scale.y|, half_h * |scale.z|)`
3. Register the shape as type `Box` with these half-extents instead of as a Cylinder

When the XY scale is uniform (within tolerance), the shape SHALL be registered as type `Cylinder` normally, with feature `(radius, half_height, 0)` independent of the world scale.

#### Scenario: Uniform scale cylinder registers normally
- **WHEN** a Cylinder shape component's owning GameObject has world scale `(2.0, 2.0, 3.0)` (uniform XY)
- **THEN** the shape is registered as `CollisionShapeType::Cylinder` with feature `(radius, half_height, 0)`
- **AND** no warning is emitted

#### Scenario: Non-uniform XY scale triggers fallback
- **WHEN** a Cylinder shape component's owning GameObject has world scale `(2.0, 1.5, 1.0)` (non-uniform XY)
- **AND** the cylinder feature specifies radius=1.0, half_height=0.5
- **THEN** `SDL_LogWarn` is emitted with the scale values
- **AND** the shape is registered as `CollisionShapeType::Box` with half_extents = `(1.0*2.0, 1.0*1.5, 0.5*1.0)` = `(2.0, 1.5, 0.5)`

#### Scenario: Deformed cylinder returns to normal on scale fix
- **WHEN** a Cylinder shape's GameObject scale is changed from `(2.0, 1.5, 1.0)` (non-uniform) to `(2.0, 2.0, 1.0)` (uniform)
- **THEN** on the next `Awake()` or update, the shape is re-registered as `CollisionShapeType::Cylinder`

### Requirement: Cylinder collision shape type constant

The `CollisionShapeType` enum SHALL include `Cylinder = 2`. The GLSL `support()` dispatch SHALL use `#define SHAPE_TYPE_CYLINDER 2u` and call `support_cylinder()` when `shape_type == SHAPE_TYPE_CYLINDER`.

#### Scenario: Cylinder type registered in physics scene
- **WHEN** a `CollisionShapeComponent` has `m_shape_type = CollisionShapeType::Cylinder` and `m_feature = {0.5, 1.0, 0}` (r=0.5, half_h=1.0)
- **THEN** `PhysicsScene::RegisterCollisionShape` stores type=2 and feature=(0.5, 1.0, 0)
- **AND** `RecalculateRigidBodyState` uses cylinder volume and inertia formulas

### Requirement: Cylinder mesh matching in SceneBuilder

`SceneBuilder::AddCylinder(const CylinderDesc&)` SHALL load the builtin `~/mesh/cylinder.asset` mesh (height 2m, radius 1m, Z-up, centered at origin) and set the mesh child's transform scale to `vec3(desc.radius, desc.radius, desc.half_height)` so the visual mesh matches the collision shape.

#### Scenario: Default cylinder scale
- **WHEN** `AddCylinder` is called with `radius = 0.5f, half_height = 0.5f`
- **THEN** the mesh child's transform scale is `(0.5, 0.5, 0.5)` (mesh: r=0.5, total height=1.0)

#### Scenario: Tall cylinder scale
- **WHEN** `AddCylinder` is called with `radius = 1.0f, half_height = 2.0f`
- **THEN** the mesh child's transform scale is `(1.0, 1.0, 2.0)` (mesh: r=1.0, total height=4.0)
