## Why

The only shadow path for directional lights is a single hard-coded orthographic shadow map. `SceneDataManager::SetLightDirectional` builds a fixed 40x40x100 box centred on the world origin with `ortho(-20, 20, -20, 20, 0.001, 100)`, and the light's direction reaches it only through `lookAtRH(..., up = (0,1,0))`, which degenerates to a NaN matrix whenever the light points straight down. The fragment shader then applies a fixed bias of `0.005` in clip space, which over that 100-unit depth range equals ~0.5 world units of displacement. The consequences are exactly the ones artists hit first: shadow coverage and texel density depend on where geometry sits relative to the world origin and on how the light is rotated, resolution has to be tuned by hand, shadows detach from their casters so objects appear to float, and there is no soft-shadow filtering at all. Commercial engines solve this with cascaded shadow maps (CSM): several shadow frusta fitted to slices of the camera frustum, so the shadow map follows the camera and the light's position stops mattering.

## What Changes

- Replace the single hard-coded directional shadow matrix with **4 cascades per shadow-casting directional light**, fitted every frame to slices of the active camera's frustum (practical split scheme, λ = 0.8, cascade 0 near plane clamped away from the camera near plane).
- Store a light's cascades in **one 2D-array depth texture per light** (D32SFLOAT, 4 layers of 2048², sampled as `sampler2DArrayShadow`), rendered by **one shadow pass per light** that loops over its cascades; storage is allocated per light when the render graph is built.
- Make the directional light's transform **position-independent and degenerate-free**: cascade fits derived from the camera and the light's rotation, a stable light basis with an axis fallback instead of a naive `up = (0,1,0)`, and texel-snapped cascade extents so shadow edges do not swim.
- Remove peter-panning: delete the fixed clip-space bias, apply a **per-cascade normal offset** scaled by the cascade's texel world size, and keep the material template's slope bias only as a fallback.
- Add **soft shadows**: 16-tap Poisson PCF with per-pixel rotation, 10% cascade transition blending, and a fade-out over the last 20% of the shadow distance.
- Cap the shadow-casting lights at **4** (`MAX_SHADOW_CASTING_LIGHTS` 8 → 4, **BREAKING**), selected brightest-first, with an explicit warning for dropped lights.
- **BREAKING** shader interface: `light_vp_matrix` becomes a flattened 16-entry array indexed `light * 4 + cascade`, a per-light `cascade_splits` vector is added, and shadow maps become `sampler2DArrayShadow`; every shader that includes `engine/interface.glsl` must be recompiled.
- **BREAKING** renderer shadow flag: `RendererComponent::m_cast_shadow` becomes effective instead of being ignored by the shadow pass, and its default flips to `true` so programmatic scenes keep their current appearance; serialized scenes that stored `false` while the flag was inert must be updated.
- Fix pre-existing hazards found while mapping the shadow path: out-of-bounds writes past `MAX_SHADOW_CASTING_LIGHTS`, and shadow-map pointers that dangle in `SceneDataManager` after a render-graph rebuild because `RemoveLightShadowMap` is never called.
- `LightComponent` gains two serialized fields: `m_shadow_distance` (default 100) and `m_cascade_count` (default 4).
- Upgrade the windowed shadow test into a large-scale CSM scene that also asserts finite cascade matrices, light-position invariance, and a clean validation layer.

## Capabilities

### New Capabilities
- `directional-shadow-cascades`: cascaded shadow maps for directional lights — camera-fitted cascade splits, per-cascade storage and sampling, normal-offset bias, PCF filtering with cascade transition blending, shadow-distance fade, the shadow-casting light budget, and the `LightComponent` fields that drive all of it.
- `renderer-shadow-cast-filter`: `RendererComponent::m_cast_shadow` decides whether a renderer is drawn into shadow maps, with a default that preserves the current "everything casts" appearance.

### Modified Capabilities
<!-- None — no existing capability's requirements change; point/spot lighting, the render graph's model-matrix contract, and the editor graph's pass structure keep their current requirements. -->

## Impact

- **Engine / Render**: `SceneDataManager` (uniform layout, cascade fitting, shadow-map registration and lifetime, light-cap enforcement), `RendererManager` filtering usage from the shadow pass, `RenderSystem::StartFrame` ordering unchanged.
- **Engine / Framework**: `WorldSystem::UpdateLightData` (light selection and fit inputs), `LightComponent` (two new serialized fields), `RendererComponent` (default flip), `ComplexRenderGraphBuilder` and the editor's `EditorRenderGraphBuilder` (shared shadow-resource construction).
- **Shaders**: `builtin_assets/shaders/include/engine/interface.glsl`, `shadowmap.vert.0.glsl`, `lambertian_cook_torrance.frag.0.glsl`, `blinn_phong.frag.0.glsl`. The stale legacy `shader/` directory is deliberately left untouched.
- **Assets / content**: `ShadowMapTemplate.asset` bias tuning; sample project levels updated for the now-effective renderer shadow flag; `test/engine/windowed/shadow_map_test.cpp` rewritten as the large-scale scene.
- **Not affected**: point and spot lights stay non-shadow-casting, `MAX_NON_SHADOW_CASTING_LIGHTS` stays 16, the RHI needs no new Vulkan features (layered depth rendering was verified against the existing attachment and view machinery), and no new graphics pipelines are created.
