# physics-scene-builder — Delta Spec

## MODIFIED Requirements

### Requirement: BoxDesc config struct
The SceneBuilder SHALL accept box configuration via a `BoxDesc` struct with the following fields, each having a sensible default: `position` (world-space center), `rotation` (world-space orientation), `half_extents` (box half-sizes), `mass`, `kinematic` flag, `static_friction` (default 0.5), `dynamic_friction` (default 0.5), `restitution` (default 0.0), and `material` (AssetRef to a material asset). The struct SHALL support C++20 designated initializers.

#### Scenario: Minimal box creation
- **WHEN** a `BoxDesc` is constructed with only `position` and `material` fields set
- **THEN** all other fields take their default values (`half_extents = {0.5,0.5,0.5}`, `mass = 1.0`, `kinematic = false`, `static_friction = 0.5`, `dynamic_friction = 0.5`, `restitution = 0.0`)

#### Scenario: Designated initializer usage
- **WHEN** calling `AddBox({.position = {0,0,5}, .material = red_mat, .kinematic = true})`
- **THEN** the compiler accepts the designated initializer syntax and the box is created with the specified overrides and defaults for all other fields

### Requirement: SceneBuilder creates fully-wired box objects
`SceneBuilder::AddBox(const BoxDesc&)` SHALL create a parent GameObject (with RigidBodyComponent, positioned at the box's world position, parented to the configured root) and two child GameObjects: one "Mesh" child with a StaticMeshComponent using the builtin cube mesh and the specified material, and one "Collision" child with a CollisionShapeComponent of Box type with `m_feature = desc.half_extents`. The RigidBodyComponent's `m_static_friction`, `m_dynamic_friction`, and `m_restitution` SHALL be set from `desc.static_friction`, `desc.dynamic_friction`, and `desc.restitution`. The mesh child's transform scale SHALL be set to `half_extents` (the builtin cube mesh is a 2×2×2 cube centered at origin, so scale maps directly to the collision half-extents). Both children SHALL have identity local transforms (position at parent origin). The StaticMeshComponent on the mesh child SHALL be tracked for later PreRenderUpdate.

#### Scenario: Box with parent-child hierarchy
- **WHEN** `AddBox` is called with a valid BoxDesc
- **THEN** the returned parent GameObject has a RigidBodyComponent; a "Mesh" child has a StaticMeshComponent with the cube mesh and specified material; a "Collision" child has a CollisionShapeComponent with `m_feature = desc.half_extents`

#### Scenario: Friction and restitution are set on rigid body
- **WHEN** `AddBox` is called with `static_friction = 0.8, dynamic_friction = 0.6, restitution = 0.3`
- **THEN** the RigidBodyComponent's `m_static_friction = 0.8`, `m_dynamic_friction = 0.6`, `m_restitution = 0.3`

#### Scenario: Kinematic ground box
- **WHEN** `AddBox` is called with `kinematic = true` and `mass = 1.0`
- **THEN** the RigidBodyComponent's `m_is_kinematic` is `true` and the box does not fall under gravity

#### Scenario: Mesh scale decoupled from collision
- **WHEN** a box has `half_extents = {2.0, 1.0, 0.5}`
- **THEN** the mesh child's transform scale is `{2.0, 1.0, 0.5}` (directly equal to half_extents, since the builtin cube is 2×2×2), while the collision child has identity local scale and `m_feature = {2.0, 1.0, 0.5}` (equal to half_extents), with no scale interference between the two

## ADDED Requirements

### Requirement: SphereDesc config struct
The SceneBuilder SHALL accept sphere configuration via a `SphereDesc` struct with the following fields, each having a sensible default: `position` (world-space center, default `{0,0,0}`), `rotation` (world-space orientation, default identity), `radius` (default 0.5), `mass` (default 1.0), `kinematic` flag (default false), `static_friction` (default 0.5), `dynamic_friction` (default 0.5), `restitution` (default 0.0), and `material` (AssetRef to a material asset). The struct SHALL support C++20 designated initializers.

#### Scenario: Minimal sphere creation
- **WHEN** a `SphereDesc` is constructed with only `position` and `material` fields set
- **THEN** all other fields take their default values (`radius = 0.5`, `mass = 1.0`, `kinematic = false`, `static_friction = 0.5`, `dynamic_friction = 0.5`, `restitution = 0.0`)

### Requirement: CylinderDesc config struct
The SceneBuilder SHALL accept cylinder configuration via a `CylinderDesc` struct with the following fields, each having a sensible default: `position` (world-space center, default `{0,0,0}`), `rotation` (world-space orientation, default identity), `radius` (default 0.5), `half_height` (default 0.5), `mass` (default 1.0), `kinematic` flag (default false), `static_friction` (default 0.5), `dynamic_friction` (default 0.5), `restitution` (default 0.0), and `material` (AssetRef to a material asset). The struct SHALL support C++20 designated initializers.

#### Scenario: Minimal cylinder creation
- **WHEN** a `CylinderDesc` is constructed with only `position` and `material` fields set
- **THEN** all other fields take their default values (`radius = 0.5`, `half_height = 0.5`, `mass = 1.0`, `kinematic = false`)

### Requirement: SceneBuilder creates fully-wired sphere objects
`SceneBuilder::AddSphere(const SphereDesc&)` SHALL create a parent GameObject (with RigidBodyComponent) and two child GameObjects: one "Mesh" child with a StaticMeshComponent using the builtin sphere mesh (radius 1m) and the specified material, with transform scale set to `vec3(desc.radius)`; and one "Collision" child with a CollisionShapeComponent of Sphere type with `m_feature = {desc.radius, 0, 0}`. The RigidBodyComponent's friction and restitution SHALL be set from the Desc.

#### Scenario: Sphere with parent-child hierarchy
- **WHEN** `AddSphere` is called with a valid SphereDesc
- **THEN** the returned parent GameObject has a RigidBodyComponent; a "Mesh" child has a StaticMeshComponent with the sphere mesh and specified material; a "Collision" child has a CollisionShapeComponent with `m_shape_type = Sphere` and `m_feature = {desc.radius, 0, 0}`

#### Scenario: Sphere mesh scale matches collision radius
- **WHEN** `AddSphere` is called with `radius = 1.5f`
- **THEN** the mesh child's transform scale is `(1.5, 1.5, 1.5)` (mesh r=1 × 1.5 = collision r=1.5)

### Requirement: SceneBuilder creates fully-wired cylinder objects
`SceneBuilder::AddCylinder(const CylinderDesc&)` SHALL create a parent GameObject (with RigidBodyComponent) and two child GameObjects: one "Mesh" child with a StaticMeshComponent using the builtin cylinder mesh (height 2m, radius 1m, Z-up, centered at origin) and the specified material, with transform scale set to `vec3(desc.radius, desc.radius, desc.half_height)`; and one "Collision" child with a CollisionShapeComponent of Cylinder type with `m_feature = {desc.radius, desc.half_height, 0}`. The RigidBodyComponent's friction and restitution SHALL be set from the Desc.

#### Scenario: Cylinder with parent-child hierarchy
- **WHEN** `AddCylinder` is called with a valid CylinderDesc
- **THEN** the returned parent GameObject has a RigidBodyComponent; a "Mesh" child has a StaticMeshComponent with the cylinder mesh and specified material; a "Collision" child has a CollisionShapeComponent with `m_shape_type = Cylinder` and `m_feature = {desc.radius, desc.half_height, 0}`

#### Scenario: Tall cylinder mesh scale
- **WHEN** `AddCylinder` is called with `radius = 0.5f, half_height = 2.0f`
- **THEN** the mesh child's transform scale is `(0.5, 0.5, 2.0)` (mesh: r=0.5, total height=4.0)

### Requirement: SceneBuilder manages mesh component tracking for all shapes
`GetMeshComponents()` SHALL return all StaticMeshComponents created by `AddBox`, `AddSphere`, and `AddCylinder` in creation order.

#### Scenario: Mixed-shape mesh tracking
- **WHEN** `AddBox`, `AddSphere`, and `AddCylinder` are each called once
- **THEN** `GetMeshComponents()` returns a vector of size 3, containing all mesh components in creation order

### Requirement: SceneBuilder loads multiple builtin meshes
The SceneBuilder constructor SHALL load the builtin cube, sphere, and cylinder meshes from `~/mesh/cube.asset`, `~/mesh/sphere.asset`, and `~/mesh/cylinder.asset` respectively. The sphere and cylinder meshes SHALL be stored as `m_sphere_mesh` and `m_cylinder_mesh` members alongside `m_cube_mesh`.

#### Scenario: All meshes loaded on construction
- **WHEN** a `SceneBuilder` is constructed with a valid `FileSystemDatabase`
- **THEN** `m_cube_mesh`, `m_sphere_mesh`, and `m_cylinder_mesh` are all valid AssetRefs
