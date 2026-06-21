## Context

The physics engine currently hardcodes box-only collision shape support. The `half_extents`/`m_box_size` naming permeates the entire pipeline — from component fields, through CPU-side PhysicsScene storage, into GPU buffers, and down to GLSL shader code. The `RecalculateRigidBodyState` function inlines box-specific volume and inertia formulas. SceneBuilder only exposes `AddBox` with no friction/restitution configuration.

We need to generalize this to support three primitive shapes (Box, Sphere, Cylinder) with a clean, type-generic data model.

**Engine conventions**: Z-up right-handed coordinate system (RFU: X right, Y front, Z up). All GLSL shaders use `set=0` with sequential bindings. Reflection/serialization uses libclang-based compile-time code generation triggered by `REFL_SER_ENABLE` annotations.

## Goals / Non-Goals

**Goals:**
- Add Sphere (type 1) and Cylinder (type 2) to `CollisionShapeType`
- Replace `half_extents`/`m_box_size` with a generic `feature` vec3 throughout the entire pipeline, and unify the box size convention to half-extents everywhere (eliminate the `×2.0`/`×0.5` round-trip between component and physics layers)
- Rename `m_box_center`→`m_center`, `m_box_rotation`→`m_rotation` in CollisionShapeComponent
- Implement correct support functions, inertia tensors, and volume formulas for all three shapes
- Extract per-shape inertia/volume into named functions
- Add `AddSphere`/`AddCylinder` to SceneBuilder with matching visual mesh transforms
- Add `static_friction`, `dynamic_friction`, `restitution` to all SceneBuilder Desc structs
- Detect non-uniform XY scale on cylinders and fall back with a warning

**Non-Goals:**
- Capsule, convex hull, triangle mesh, or compound shapes
- Shape-specific contact manifold optimizations (the existing perturbation pipeline works for all convex shapes)
- Migration of existing serialized scene data (no scenes exist in the wild yet)
- Changing the GPU buffer layout (SoA stays; only names and enum values change)
- Supporting elliptical cylinders
- Adding friction/restitution to the component level (already on RigidBodyComponent)

## Decisions

### Decision 1: Feature vec3 encoding

Each shape's geometric parameters are packed into a single `vec3 feature`:

| Shape | feature.x | feature.y | feature.z |
|-------|-----------|-----------|-----------|
| Box (0) | half_extent_x | half_extent_y | half_extent_z |
| Sphere (1) | radius | unused (0) | unused (0) |
| Cylinder (2) | radius | half_height (Z) | unused (0) |

**Rationale**: A single `vec3` keeps the GPU buffer layout unchanged (still `vec4 v[]` per shape slot). The type enum already tells us how to interpret the payload. No new buffers needed.

**Alternative considered**: Separate buffers per shape type — rejected because it complicates buffer management and the SoA layout for mixed-shape scenes. A union-like approach in a single buffer is simpler and sufficient.

### Decision 2: Cylinder orientation (Z-up)

The cylinder height axis is Z, consistent with the engine's Z-up convention (see `Transform.h` docs). The support function decomposes a direction into Z (axial) and XY (radial) components in local space.

The builtin `cylinder.asset` mesh is modeled with height along Z (total height 2m, radius 1m, centered at origin). SceneBuilder sets mesh scale to `(radius, radius, half_height)` → for a default cylinder (r=1, half_h=1), scale is `(1, 1, 1)` which matches the mesh exactly.

**Rationale**: Z-up matches the engine convention. The alternative (Y-up cylinders) would require a rotation to make the mesh match the collision, adding confusion.

### Decision 3: Non-uniform cylinder scale detection

In `CollisionShapeComponent::Awake()`, when `m_shape_type == Cylinder`, we extract the world transform scale from the owning GameObject. If `|scale.x - scale.y| > 1e-4f` (indicating non-uniform XY scaling that would distort the circular cross-section), we:

1. Log an `SDL_LogWarn` with the GameObject name and scale values
2. Compute a bounding box approximation: `half_extents = (r * |sx|, r * |sy|, half_h * |sz|)`
3. Fall back to registering the shape as type `Box` with these half-extents

**Rationale**: The cylinder support function assumes a circular cross-section (`r` applied uniformly to X and Y). Non-uniform XY scale would produce an elliptical cylinder which requires a fundamentally different support function. A bounding box is a conservative approximation that prevents incorrect collision results.

### Decision 4: Inertia function extraction

Per-shape inertia and volume are extracted into functions in an anonymous namespace in `PhysicsScene.cpp`:

```cpp
namespace {
    float ComputeBoxVolume(const glm::vec3 &half_extents);
    float ComputeSphereVolume(float radius);
    float ComputeCylinderVolume(float radius, float half_height);

    glm::mat3 ComputeBoxInertia(float mass, const glm::vec3 &half_extents);
    glm::mat3 ComputeSphereInertia(float mass, float radius);
    glm::mat3 ComputeCylinderInertia(float mass, float radius, float half_height);
} // namespace
```

The cylinder inertia uses Z-up formulas:
- I_zz (axial) = (1/2) × m × r²
- I_xx = I_yy (transverse) = (1/12) × m × (3r² + h²) where h = 2 × half_height

`RecalculateRigidBodyState` switches on `m_shape_type[shape_index]` to call the appropriate functions instead of inline box calculations.

**Rationale**: Clean separation of concerns. Each function is independently testable with known inputs/outputs. The dispatch in `RecalculateRigidBodyState` is a simple switch with 3 cases.

### Decision 5: Buffer / field rename strategy

`half_extents` → `feature` everywhere:

| Layer | Old Name | New Name |
|-------|----------|----------|
| Component field | `m_box_size` | `m_feature` |
| Component field | `m_box_center` | `m_center` |
| Component field | `m_box_rotation` | `m_rotation` |
| PhysicsScene CPU | `m_shape_half_extents` | `m_shape_feature` |
| PhysicsScene GPU ptr | `shape_half_extents` | `shape_feature` |
| GLSL buffer | `ShapeHalfExtents` | `ShapeFeature` |
| GLSL variable | `shape_half_extents` | `shape_feature` |
| ConvexCollisionDetector | `"ShapeHalfExtents"` | `"ShapeFeature"` |
| Render graph handle | `shape_half_extents_handle` | `shape_feature_handle` |

The rename is mechanical (find-replace with exact matches). All affected locations are in the files listed in the impact section.

**BREAKING**: Serialized scene data that references `m_box_size`, `m_box_center`, `m_box_rotation` will fail to load. The user has confirmed no production scenes exist yet.

### Decision 5b: Box size convention unified to half-extents

Currently there is a confusing dual convention where `m_box_size` stores **full** box dimensions but the PhysicsScene layer operates on **half-extents**. Two conversions bridge the gap:

```
BoxDesc.half_extents → [×2.0 in SceneBuilder] → m_box_size (full) → [×0.5 in Awake] → PhysicsScene (half)
```

This round-trip is eliminated:

```
BoxDesc.half_extents → m_feature (half-extents) → PhysicsScene (half-extents)
```

Concretely:
- `CollisionShapeComponent::m_feature` default changes from `{1.0, 1.0, 1.0}` (full) to `{0.5, 0.5, 0.5}` (half-extents)
- `SceneBuilder::AddBox` sets `shape.m_feature = desc.half_extents` (no `×2.0`)
- `CollisionShapeComponent::Awake()` passes `m_feature` directly for Box (no `×0.5`)
- Test values are halved: `{2.0, 2.0, 2.0}` → `{1.0, 1.0, 1.0}`, `{1.0, 1.0, 1.0}` → `{0.5, 0.5, 0.5}`

**Locations affected** (14 total):

| File | Change |
|------|--------|
| `CollisionShapeComponent.h:72` | Default `{1,1,1}` → `{0.5,0.5,0.5}` |
| `CollisionShapeComponent.cpp:47` | Remove `* 0.5f` from `UpdateCollisionShapeGeometry` call |
| `CollisionShapeComponent.cpp:51` | Remove `* 0.5f` from `RegisterCollisionShape` call |
| `SceneBuilder.cpp:80` | `desc.half_extents * 2.0f` → `desc.half_extents` |
| `physics_registration_test.cpp:89` | `{2,2,2}` → `{1,1,1}` |
| `physics_registration_test.cpp:92,96,99,102` | `{1,1,1}` → `{0.5,0.5,0.5}` (×4 occurrences) |

**Rationale**: All three shape types now store their natural parameters directly in `m_feature` without any conversion — Box stores half-extents, Sphere stores radius, Cylinder stores (radius, half_height). This eliminates confusion about which layer uses which convention.

### Decision 6: SceneBuilder API design

**New Desc structs**:

```cpp
struct SphereDesc {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float radius{0.5f};
    float mass{1.0f};
    bool kinematic{false};
    float static_friction{0.5f};
    float dynamic_friction{0.5f};
    float restitution{0.0f};
    AssetRef material{};
};

struct CylinderDesc {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float radius{0.5f};
    float half_height{0.5f};
    float mass{1.0f};
    bool kinematic{false};
    float static_friction{0.5f};
    float dynamic_friction{0.5f};
    float restitution{0.0f};
    AssetRef material{};
};
```

`BoxDesc` gains the same three friction fields with the same defaults.

**Mesh loading**: `SceneBuilder` constructor loads `sphere.asset` and `cylinder.asset` alongside the existing `cube.asset`, stored as `m_sphere_mesh` and `m_cylinder_mesh`.

**Mesh transforms**:

| Shape | Mesh | Scale | Rationale |
|-------|------|-------|-----------|
| Box | cube (2×2×2) | `half_extents` | 2·scale = 2·he = full size |
| Sphere | sphere (r=1) | `vec3(radius)` | 1·scale = radius |
| Cylinder | cylinder (r=1, h=2, Z-up) | `vec3(radius, radius, half_height)` | 1·scale.x = r, 2·scale.z = 2·hh = h |

**Friction defaults**: Match `RigidBodyComponent` defaults (static=0.5, dynamic=0.5, restitution=0.0) so behavior is consistent when users don't specify them.

### Decision 7: GLSL support function for sphere and cylinder

**Sphere** (trivial — the farthest point is always `center + normalize(dir) × radius`):

```glsl
vec3 support_sphere(vec3 feature, vec3 world_pos, vec4 world_rot, vec3 dir_world) {
    float r = feature.x;
    float len = length(dir_world);
    if (len < 1e-8) return world_pos;
    return world_pos + (dir_world / len) * r;
}
```

Note: sphere support ignores rotation (sphere is rotationally invariant), so `world_rot` is unused.

**Cylinder** (Z-up, decompose into axial + radial):

```glsl
vec3 support_cylinder(vec3 feature, vec3 world_pos, vec4 world_rot, vec3 dir_world) {
    float r = feature.x;
    float half_h = feature.y;
    vec3 dir_local = quat_inv_rotate(world_rot, dir_world);
    
    // Z (axial) component
    float z_sign = (dir_local.z >= 0.0) ? half_h : -half_h;
    
    // XY (radial) component
    vec2 dir_xy = vec2(dir_local.x, dir_local.y);
    float len_xy = length(dir_xy);
    vec2 radial = (len_xy > 1e-8) ? (dir_xy / len_xy) * r : vec2(0.0);
    
    vec3 local_support = vec3(radial.x, radial.y, z_sign);
    return quat_rotate(world_rot, local_support) + world_pos;
}
```

The dispatch in `support()` uses if-else on shape type constants:
```glsl
#define SHAPE_TYPE_BOX      0u
#define SHAPE_TYPE_SPHERE   1u
#define SHAPE_TYPE_CYLINDER 2u
```

## Risks / Trade-offs

- **[Risk] Cylinder non-uniform scale fallback silently changes collision shape** → Mitigation: `SDL_LogWarn` is emitted; the warning includes the GameObject identifier. In the GLSL shader, if a cylinder shape somehow reaches the GPU with non-uniform scale undetected, the support function still returns a valid point (just an incorrect one for elliptical cross-section). The CPU-side check in `Awake()` is the primary guard.

- **[Risk] GPU `debugPrintfEXT` for cylinder scale warning requires Vulkan debug extension** → Mitigation: The warning is primarily logged CPU-side (SDL_LogWarn). A GPU-side `debugPrintfEXT` is an additional diagnostic but not the sole mechanism.

- **[Risk] Serialization breaking change** → Mitigation: No production scenes exist. The generated serialization code auto-updates when the header is re-parsed. If needed later, a migration pass could rename old JSON keys.

- **[Trade-off] Single `feature` vec3 cannot represent all possible shape parameters** (e.g., capsule needs two radii + height) → Future shapes may need additional buffers. This is acceptable — the current vec3 covers Box/Sphere/Cylinder cleanly, and adding a `shape_feature2` buffer later is a non-breaking extension.

## Open Questions

_(None at this time — all design choices are resolved per user direction.)_
