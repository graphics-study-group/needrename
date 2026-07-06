## Why

The physics example (`example/physics_example/main.cpp`) currently requires ~10 lines of boilerplate per box object for GameObject creation, transform setup, rigid body registration, collision shape configuration, and visual mesh assignment. All objects share the same sphere mesh and material, making them visually indistinguishable during simulation. We need a simpler scene construction API and a light-weight solid-color material system that doesn't require per-object texture assets.

## What Changes

- **Add `albedoFactor` uniform to the PBR fragment shader** — a `vec4` in the Material UBO that multiplies with the sampled albedo texture, enabling a single white texture to produce any solid color via factor modulation
- **Create ~9 preset solid color material assets** in `builtin_assets/materials/` — each references the shared white texture and sets a different `albedoFactor` value (red, green, blue, yellow, cyan, magenta, orange, white, grey), eliminating the need for per-object material authoring
- **Create `SceneBuilder` helper class** in `example/physics_example/` — encapsulates GameObject creation, component wiring, mesh assignment, and mesh tracking behind a concise `BoxDesc` config struct API
- **Simplify `main.cpp`** — replace verbose per-box setup with SceneBuilder calls, reducing scene construction code by ~60%

## Capabilities

### New Capabilities
- `pbr-albedo-factor`: A `vec4 albedoFactor` uniform in the PBR Material UBO that multiplies the sampled albedo texture, allowing solid-color materials via a shared white texture
- `physics-scene-builder`: A SceneBuilder helper class in the physics example that provides a concise, config-driven API for adding physics boxes with automatic component wiring and visual mesh assignment

### Modified Capabilities
<!-- None — this change does not modify existing engine-level spec behavior -->

## Impact

- **Shader**: `lambertian_cook_torrance.frag.0.glsl` gains one `vec4` uniform; `.spv` must be recompiled
- **Material library**: PBR material template remains unchanged (shader reflection picks up the new uniform automatically)
- **Builtin assets**: 9 new `.asset` files in `builtin_assets/materials/`, plus any new mesh/cube asset references
- **Example code**: New files `SceneBuilder.h`/`.cpp`; `main.cpp` rewritten to use the builder
- **Engine core**: No changes — all new code is in example or asset directories
