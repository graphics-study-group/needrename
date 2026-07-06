## Why

The physics engine currently only supports box collision shapes. Adding sphere and cylinder shapes is the natural next step for building richer physics scenes — spheres for balls, projectiles, and rolling objects; cylinders for pillars, barrels, and wheels. These are the two most common primitives after boxes. This change also cleans up the shape data model by replacing box-specific naming (`half_extents`, `m_box_size`) with a generic `feature` vec3 that adapts to each shape type, and adds friction/restitution configuration to the SceneBuilder.

## What Changes

- **New collision shape types**: `Sphere` and `Cylinder` added to `CollisionShapeType` enum, with corresponding GPU support functions, inertia formulas, and volume calculations
- **Generic shape feature**: `half_extents`/`m_box_size` renamed to `feature` throughout the entire pipeline (component → CPU physics → GPU buffers → GLSL shaders), a `vec3` whose interpretation depends on shape type. **Box size convention unified to half-extents everywhere** — the component now stores half-extents directly (eliminating the previous `×2.0`/`×0.5` round-trip between SceneBuilder and Awake)
- **CollisionShapeComponent field rename**: `m_box_size` → `m_feature`, `m_box_center` → `m_center`, `m_box_rotation` → `m_rotation` — **BREAKING** for any serialized scene data referencing the old field names
- **Dedicated inertia functions**: Shape inertia computation extracted into per-type functions (`ComputeBoxInertia`, `ComputeSphereInertia`, `ComputeCylinderInertia`) for clarity and testability
- **Non-uniform cylinder fallback**: Cylinder support function detects non-uniform XY scaling; falls back to a bounding box approximation with a runtime warning
- **SceneBuilder extended**: `AddSphere(SphereDesc)` and `AddCylinder(CylinderDesc)` methods; all Desc structs gain `static_friction`, `dynamic_friction`, `restitution` fields with sensible defaults
- **Z-up orientation**: Cylinder height is along the Z axis, consistent with the engine's Z-up convention

## Capabilities

### New Capabilities

- `sphere-collision-shape`: Sphere primitive collision shape — GPU support function returning `center + normalize(dir) * radius`, solid-sphere inertia tensor `(2/5)mr²·I`, volume `(4/3)πr³`, and SceneBuilder integration using the builtin `sphere.asset` mesh (radius 1m)
- `cylinder-collision-shape`: Cylinder primitive collision shape (Z-up) — GPU support function decomposing direction into Z-axis and radial components, solid-cylinder inertia tensor (axial `(1/2)mr²`, transverse `(1/12)m(3r²+h²)` where h = total height), volume `πr²h`, and SceneBuilder integration using the builtin `cylinder.asset` mesh (height 2m, radius 1m)
- `generic-shape-feature`: A single `vec3 feature` field replaces `half_extents`/`m_box_size` across the entire physics pipeline, interpreted per shape type (Box: half-extents xyz; Sphere: radius in x; Cylinder: radius in x, half-height in y)
- `shape-inertia-functions`: Per-shape-type inertia and volume computation extracted into named functions with clear formulas, replacing inline box-only code in `RecalculateRigidBodyState`

### Modified Capabilities

- `physics-scene-builder`: SceneBuilder extended with `AddSphere` and `AddCylinder`; BoxDesc/SphereDesc/CylinderDesc gain `static_friction`, `dynamic_friction`, `restitution` fields; mesh tracking supports non-box meshes
- `gpu-convex-collision-detection`: Support function dispatch in `support.glsl` extended for Sphere (type 1) and Cylinder (type 2); shape buffer `ShapeHalfExtents` renamed to `ShapeFeature`
- `physics-gpu-shaders`: `ShapeHalfExtents` SSBO renamed to `ShapeFeature` in `detect_collisions.comp` and all binding sites; cylinder non-uniform-scale detection may emit `debugPrintfEXT` warning

## Impact

- **Component layer**: `CollisionShapeComponent.h/.cpp` — field renames, new type handling in `Awake()`
- **Physics CPU**: `PhysicsScene.h/.cpp` — enum, buffer renames, inertia refactor, volume calcs
- **Collision detection**: `ConvexCollisionDetector.cpp` — buffer binding name sync
- **GPU shaders**: `support.glsl` (new functions), `detect_collisions.comp` (buffer rename)
- **Example**: `SceneBuilder.h/.cpp` — new methods, new Desc structs, friction fields
- **Tests**: `physics_registration_test.cpp` — field name updates
- **Serialization**: Generated `__generated__/` files auto-regenerate; component schema change is **BREAKING** for existing serialized scene data (no migration — confirmed acceptable by user)
- **Build**: New shader code in `support.glsl` recompiles to SPIR-V via existing `physics_shader` CMake target (no CMake changes needed)
