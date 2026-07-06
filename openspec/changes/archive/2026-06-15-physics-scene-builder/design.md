## Context

The physics example currently constructs scenes with verbose, manual per-object setup in `main.cpp`. Each box requires explicit GameObject creation, transform setup, RigidBodyComponent/CollisionShapeComponent wiring, and StaticMeshComponent assignment — all with hardcoded sphere meshes and a single shared material. The PBR shader's Material UBO has no albedo color factor, so creating colored materials requires separate texture assets per color, which is overly heavy for simple debugging/prototyping scenes.

## Goals / Non-Goals

**Goals:**
- Provide a concise, config-driven API (`SceneBuilder`) for adding physics boxes in one function call
- Enable solid-color materials via a single `albedoFactor` shader uniform, reusing one white texture
- Ship ~9 preset solid-color material assets so users can pick colors without authoring assets
- Use the builtin cube mesh for box visual representation (matching collision geometry)
- Keep all SceneBuilder code in the example directory — zero engine changes required

**Non-Goals:**
- Supporting shapes other than box (sphere, capsule, etc.) — this is deferred
- Runtime material creation or color manipulation via code — colors come from preset `.asset` files
- Editor integration or serialization of the builder config
- Custom textures or PBR parameter overrides per object beyond the preset materials
- Replacing the existing component-based scene construction path — SceneBuilder is an optional convenience, not a new requirement

## Decisions

### Decision 1: albedoFactor in the Material UBO (not a separate texture)

**Chosen**: Add `vec4 albedoFactor` to the existing `Material` uniform block (`set=2, binding=0`) in `lambertian_cook_torrance.frag.0.glsl`, and multiply it with the sampled albedo texture.

**Alternatives considered**:
- Per-object SolidColorTextureAsset creation at runtime: requires writing asset serialization code, creates many small textures, and mixes asset management with scene construction.
- Separate "flat color" shader variant: doubles shader maintenance burden for a trivial change.
- Hardcoded color array in the shader with per-object index: fragile, requires GPU buffer plumbing.

**Why this approach**: The Material UBO is already reflected by the shader reflection system. Adding one `vec4` requires a 2-line shader change and zero engine C++ changes. All existing material assets continue to work — the field defaults to (1,1,1,1) if not set, making it backward compatible.

### Decision 2: Preset material assets (not runtime material construction)

**Chosen**: Create ~9 static `.asset` files in `builtin_assets/materials/`, each being a PBR material with white albedo texture and a different `albedoFactor` value.

**Alternatives considered**:
- Runtime MaterialInstance creation in C++: requires access to MaterialLibrary, manual UBO parameter setting, and bypasses the asset pipeline.
- The Explore discussion's "random color" approach: adds complexity for marginal gain in a debugging/prototyping context.

**Why this approach**: Declarative assets are easier to inspect, edit, and version control. Users can add more colors by duplicating a 30-line JSON file. No new C++ material-creation API surface.

### Decision 3: SceneBuilder as an example-local helper (not an engine utility)

**Chosen**: Place `SceneBuilder.h/.cpp` inside `example/physics_example/`.

**Alternatives considered**:
- Engine-level `PhysicsSceneBuilder` in `engine/Physics/`: premature generalization. The engine's API is the component system; SceneBuilder is example sugar.
- Inline helpers in `main.cpp`: works for 2-3 objects but doesn't scale, and provides no reusable pattern for other examples.

**Why this approach**: Keeps the engine API surface clean. If multiple examples need the same builder pattern, we can promote it later.

### Decision 4: Config struct (BoxDesc) API style

**Chosen**: A `BoxDesc` struct with designated-initializer-friendly fields, passed to `SceneBuilder::AddBox()`.

```cpp
struct BoxDesc {
    glm::vec3 position{0, 0, 0};
    glm::vec3 half_extents{0.5, 0.5, 0.5};
    float mass = 1.0f;
    bool kinematic = false;
    AssetRef material{};  // preset solid color material
};
```

**Alternatives considered**:
- Positional parameters: `AddBox(pos, size, mass, ...)` — fragile with many params, no self-documentation at call sites.
- Builder pattern (method chaining): over-engineered for what is essentially a struct initializer.

**Why this approach**: C++20 designated initializers make call sites read like a DSL. Adding a new field doesn't break existing calls. The compiler enforces that all fields have explicit values or defaults.

### Decision 5: Parent-child hierarchy for box objects

**Chosen**: `AddBox` creates a parent GameObject with the RigidBodyComponent and two child GameObjects — one for the visual mesh (StaticMeshComponent) and one for the collision shape (CollisionShapeComponent).

```
Parent GO (RigidBodyComponent)
├── Child: "Mesh" GO (StaticMeshComponent, cube mesh)
│   └── Transform scale = half_extents
│       (cube is 2×2×2 centered at origin, so scale maps directly to half_extents)
└── Child: "Collision" GO (CollisionShapeComponent)
    └── m_box_size = half_extents
```

All three GOs share the same world position (children have identity local transforms). The mesh child's scale does not affect the collision geometry because they are on separate GameObjects.

**Alternatives considered**:
- Single GO with all components + transform scale for the mesh: the scale applied to the GO also affects the collision shape's world-space transform, causing collision geometry and visual mesh to misalign.
- Offsetting the mesh vertex data: requires a custom cube mesh asset per box size, wasteful.

**Why this approach**: `RigidBodyComponent::Awake()` already recursively collects `CollisionShapeComponent` from descendant GameObjects, so the engine naturally supports this pattern. The mesh and collision transforms are fully decoupled, guaranteeing they stay aligned without any per-frame correction.

### Risks / Trade-offs

- **[Resolved] Collision-visual misalignment**: Scaling a single GO affects both mesh and collision transforms. Mitigation: parent-child hierarchy (Decision 5 above) completely decouples the two concerns.
- **[Low] Shader recompile required**: Adding a uniform to the Material UBO changes the SPIR-V binary. The `.spv` must be regenerated. Mitigation: the build system already handles shader compilation; just rebuild.
- **[Low] albedoFactor default**: Existing materials that don't set `albedoFactor` will get whatever the UBO default is. Mitigation: the shader reflection system uses zero-initialization for unspecified fields, so we must ensure the shader defaults to `vec4(1.0)` or handle the zero case in GLSL as `max(albedoFactor, vec4(0.001))` or simply initialize the field to `(1,1,1,1)` in all new `.asset` files.
- **[Low] Backward compat for existing materials**: `red_brick.asset` and `solid_color_dark_grey.asset` don't have `albedoFactor` in their properties. The asset loader will leave the UBO field at its default. Solution: ensure the shader treats missing/zero albedoFactor as `(1,1,1,1)` — this is the natural behavior if we default to white.
