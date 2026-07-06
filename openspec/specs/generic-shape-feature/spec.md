# generic-shape-feature

## Purpose

Replace box-specific naming (`half_extents`, `m_box_size`, `m_box_center`, `m_box_rotation`) with a type-generic `feature` vec3 and generic `center`/`rotation` field names throughout the entire physics pipeline — from the CollisionShapeComponent, through PhysicsScene CPU storage, GPU buffers, and GLSL shaders.

## Requirements

### Requirement: CollisionShapeComponent uses generic field names

`CollisionShapeComponent` SHALL expose the following `REFL_SER_ENABLE` fields:
- `m_shape_type` (`CollisionShapeType`, default `Box`)
- `m_feature` (`glm::vec3`, default `{0.5, 0.5, 0.5}` — box half-extents)
- `m_center` (`glm::vec3`, default `{0, 0, 0}`)
- `m_rotation` (`glm::quat`, default identity)

The fields `m_box_size`, `m_box_center`, and `m_box_rotation` SHALL be removed (renamed).

#### Scenario: Box component with new field names
- **WHEN** a `CollisionShapeComponent` is created with `m_shape_type = Box`, `m_feature = {1.0, 0.5, 0.25}`
- **THEN** `Awake()` passes `m_feature` directly as half-extents `{1.0, 0.5, 0.25}` to PhysicsScene (no conversion)

#### Scenario: Sphere component with feature
- **WHEN** a `CollisionShapeComponent` is created with `m_shape_type = Sphere`, `m_feature = {1.5, 0, 0}`
- **THEN** `Awake()` extracts `radius = m_feature.x` and registers with PhysicsScene using feature `(1.5, 0, 0)`

#### Scenario: Serialization uses new field names
- **WHEN** a `CollisionShapeComponent` is serialized to JSON
- **THEN** the JSON keys are `"CollisionShapeComponent::m_shape_type"`, `"CollisionShapeComponent::m_feature"`, `"CollisionShapeComponent::m_center"`, `"CollisionShapeComponent::m_rotation"`
- **AND** no `m_box_size`, `m_box_center`, or `m_box_rotation` keys appear

### Requirement: PhysicsScene uses generic storage names

`PhysicsScene` SHALL rename its internal storage:
- `m_shape_half_extents` → `m_shape_feature` (still `std::vector<glm::vec4>`)
- GPU buffer pointer `shape_half_extents` → `shape_feature`

All method parameters named `half_extents` SHALL be renamed to `feature`. Documentation comments SHALL be updated to describe the generic feature payload.

#### Scenario: RegisterCollisionShape with feature parameter
- **WHEN** `RegisterCollisionShape(handle, type, feature, world_pos, world_rot)` is called with `type = Box, feature = {1, 2, 3}`
- **THEN** `m_shape_feature[shape_index]` stores `vec4(1, 2, 3, 0)`

#### Scenario: GPU buffer pointer uses new name
- **WHEN** `PhysicsScene::GetGpuBuffers()` is called
- **THEN** the returned `PhysicsGpuBuffers` struct has `shape_feature` (not `shape_half_extents`)

### Requirement: GLSL ShapeFeature buffer replaces ShapeHalfExtents

The collision detection compute shader `detect_collisions.comp` SHALL declare:
```glsl
layout(set = 0, binding = 2) readonly buffer ShapeFeature { vec4 v[]; } shape_feature;
```
replacing the previous `ShapeHalfExtents` / `shape_half_extents` declaration. The `support.glsl` dispatch SHALL read `shape_feature.v[shape_index].xyz` instead of `shape_half_extents.v[shape_index].xyz`.

#### Scenario: Box support reads from renamed buffer
- **WHEN** `support()` is called for a Box shape
- **THEN** it reads `shape_feature.v[shape_index].xyz` to get the half-extents

#### Scenario: Sphere support reads from renamed buffer
- **WHEN** `support()` is called for a Sphere shape
- **THEN** it reads `shape_feature.v[shape_index].x` as the radius

### Requirement: ConvexCollisionDetector uses updated buffer binding name

`ConvexCollisionDetector::Step()` SHALL bind the feature buffer as `"ShapeFeature"` (not `"ShapeHalfExtents"`). Render graph resource handle variables SHALL be renamed accordingly.

#### Scenario: Buffer bound with new name
- **WHEN** `detect_srb.BindBuffer("ShapeFeature", *gpu.shape_feature)` is called
- **THEN** the GLSL `ShapeFeature` buffer declaration receives the GPU data

### Requirement: CollisionShapeComponent::Awake computes feature per type

`CollisionShapeComponent::Awake()` SHALL pass `m_feature` directly to PhysicsScene for all shape types:
- **Box**: `feature = m_feature` (component stores half-extents directly)
- **Sphere**: `feature = m_feature` (component stores radius directly in x)
- **Cylinder**: `feature = m_feature` (component stores radius in x, half_height in y directly)

#### Scenario: Box feature passthrough
- **WHEN** a Box component has `m_feature = {1.0, 1.0, 1.0}` (half-extents)
- **THEN** the feature passed to PhysicsScene is `{1.0, 1.0, 1.0}` (no conversion)

#### Scenario: Sphere feature passthrough
- **WHEN** a Sphere component has `m_feature = {1.5, 0, 0}` (radius = 1.5)
- **THEN** the feature passed to PhysicsScene is `{1.5, 0, 0}`

#### Scenario: Cylinder feature passthrough
- **WHEN** a Cylinder component has `m_feature = {0.5, 1.0, 0}` (r=0.5, half_h=1.0)
- **THEN** the feature passed to PhysicsScene is `{0.5, 1.0, 0}`

### Requirement: GetLocalCenterInParentSpace and GetLocalRotationInParentSpace use new field names

`GetLocalCenterInParentSpace()` SHALL return `m_center`. `GetLocalRotationInParentSpace()` SHALL return `m_rotation`.

#### Scenario: Center getter
- **WHEN** `GetLocalCenterInParentSpace()` is called on a component with `m_center = {1, 2, 3}`
- **THEN** the return value is `{1, 2, 3}`
