## ADDED Requirements

### Requirement: BoxDesc config struct
The SceneBuilder SHALL accept box configuration via a `BoxDesc` struct with the following fields, each having a sensible default: `position` (world-space center), `half_extents` (box half-sizes), `mass`, `kinematic` flag, and `material` (AssetRef to a material asset). The struct SHALL support C++20 designated initializers.

#### Scenario: Minimal box creation
- **WHEN** a `BoxDesc` is constructed with only `position` and `material` fields set
- **THEN** all other fields take their default values (`half_extents = {0.5,0.5,0.5}`, `mass = 1.0`, `kinematic = false`)

#### Scenario: Designated initializer usage
- **WHEN** calling `AddBox({.position = {0,0,5}, .material = red_mat, .kinematic = true})`
- **THEN** the compiler accepts the designated initializer syntax and the box is created with the specified overrides and defaults for all other fields

### Requirement: SceneBuilder creates fully-wired box objects
`SceneBuilder::AddBox(const BoxDesc&)` SHALL create a parent GameObject (with RigidBodyComponent, positioned at the box's world position, parented to the configured root) and two child GameObjects: one "Mesh" child with a StaticMeshComponent using the builtin cube mesh and the specified material, and one "Collision" child with a CollisionShapeComponent of Box type with `m_box_size = desc.half_extents`. The mesh child's transform scale SHALL be set to `half_extents` (the builtin cube mesh is a 2×2×2 cube centered at origin, so scale maps directly to the collision half-extents). Both children SHALL have identity local transforms (position at parent origin). The StaticMeshComponent on the mesh child SHALL be tracked for later PreRenderUpdate.

#### Scenario: Box with parent-child hierarchy
- **WHEN** `AddBox` is called with a valid BoxDesc
- **THEN** the returned parent GameObject has a RigidBodyComponent; a "Mesh" child has a StaticMeshComponent with the cube mesh and specified material; a "Collision" child has a CollisionShapeComponent with `m_box_size = desc.half_extents`

#### Scenario: Kinematic ground box
- **WHEN** `AddBox` is called with `kinematic = true` and `mass = 1.0`
- **THEN** the RigidBodyComponent's `m_is_kinematic` is `true` and the box does not fall under gravity

#### Scenario: Mesh scale decoupled from collision
- **WHEN** a box has `half_extents = {2.0, 1.0, 0.5}`
- **THEN** the mesh child's transform scale is `{2.0, 1.0, 0.5}` (directly equal to half_extents, since the builtin cube is 2×2×2), while the collision child has identity local scale and `m_box_size = {2.0, 1.0, 0.5}`, with no scale interference between the two

### Requirement: SceneBuilder manages mesh component tracking
`SceneBuilder` SHALL internally maintain a list of all StaticMeshComponents created via `AddBox`, and expose it via `GetMeshComponents()` for PreRenderUpdate registration. `SceneBuilder::Finalize(PhysicsScene&)` SHALL call `InitializePendingRigidBodies`.

#### Scenario: PreRenderUpdate ready
- **WHEN** all boxes have been added and `GetMeshComponents()` is called
- **THEN** the returned vector contains all StaticMeshComponents in creation order, ready for `PreRenderUpdate()` iteration

#### Scenario: Physics initialization
- **WHEN** `Finalize(physics_scene)` is called
- **THEN** `physics_scene.InitializePendingRigidBodies(render_system)` is invoked, and all rigid bodies are ready for simulation
