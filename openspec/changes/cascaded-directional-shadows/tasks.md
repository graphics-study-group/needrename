## 1. Interface Constants and Light Uniform Layout

- [x] 1.1 Change `MAX_SHADOW_CASTING_LIGHTS` from 8 to 4 in `engine/Render/RenderSystem/SceneDataManager.h` and the matching `#define` in `builtin_assets/shaders/include/engine/interface.glsl`; add a `MAX_SHADOW_CASCADES` constant (4) to both. Verify by grepping both files for the two constants and confirming the values agree (the header comment requires them to stay in sync)
- [x] 1.2 Restructure the shadow-casting light uniform block in `SceneDataManager.cpp`: `light_vp_matrix` becomes a flattened `mat4[MAX_SHADOW_CASTING_LIGHTS * MAX_SHADOW_CASCADES]`, add `vec4 cascade_splits[MAX_SHADOW_CASTING_LIGHTS]` and a per-light cascade count. Verify by asserting `sizeof` of the block matches the GLSL `std140` layout expectation in a static_assert or debug check
- [x] 1.3 Update `interface.glsl` to the new layout: `sampler2DArrayShadow light_shadowmaps[MAX_SHADOW_CASTING_LIGHTS]`, flattened cascade matrices, cascade splits and counts. Verify the shader compile test target builds successfully (requires build)
- [x] 1.4 Fix the light-count guards in `SceneDataManager`: the `SetLightCount` assertion must accept exactly the maximum, and out-of-range indices must be rejected instead of written. Verify by a debug-build run with a scene containing exactly 4 shadow-casting directional lights (no assertion, no out-of-range write)
- [x] 1.5 Implement brightest-first selection of shadow-casting directional lights in `WorldSystem::UpdateLightData` (sort by colour × intensity, keep the top 4, log a warning naming the dropped lights). Verify by a scene with 5 shadow-casting directional lights: 4 cast, one warning logged, no crash

## 2. Light Component Configuration Fields

- [x] 2.1 Add `REFL_SER_ENABLE float m_shadow_distance{100.0f}` and `REFL_SER_ENABLE uint32_t m_cascade_count{4}` to `LightComponent.h`. Verify by regenerating reflection/serialization files (`__generated__` includes) and building (requires build)
- [x] 2.2 Clamp both fields where they are consumed (distance > 0, cascade count within 1..4) so out-of-range serialized values cannot reach the fit. Verify by loading a light asset with `m_cascade_count` 0 and 9 and confirming it renders with a clamped count

## 3. Cascade Fit

- [x] 3.1 Implement the practical split scheme (λ = 0.8) with cascade 0's near plane clamped to `max(camera_near, shadow_distance × 0.001)` and write the four split distances into the light's `cascade_splits`. Verify against the design's worked example for shadow distance 100 (splits approximately 0.1 / 5.5 / 12.5 / 29 / 100) via a debug log of the computed splits
- [x] 3.2 Implement the per-cascade fit: transform the eight corners of each camera sub-frustum slice into light space, build the tight orthographic projection from the resulting AABB, extend the near plane toward the light by the cascade box's world width, and grow the lateral extents by 5%. Verify that a caster standing outside the slice but in front of it still casts into that cascade (large test scene, visual)
- [x] 3.3 Implement the stable light basis: world Y as the reference up axis with a switch to world Z when `|dot(direction, Y)| > 0.99`. Verify that a light rotated to point straight down produces finite matrices and renders shadows (no NaN)
- [x] 3.4 Snap the light-space orthographic extents to whole texels in X/Y only. Verify that panning the camera across the test scene leaves shadow edges stable instead of crawling
- [x] 3.5 Replace the hard-coded matrix in `SceneDataManager::SetLightDirectional` with the cascade fit, extending its inputs to receive the active camera's view matrix and near/far, and remove the stale TODO comment. Verify that `WorldSystem::UpdateLightData` passes the active camera and that no camera leaves the previous frame's cascade data untouched
- [x] 3.6 Guard the fit against non-finite output (degenerate direction, zero-size slice) by falling back to the previous frame's cascades. Verify by rotating the light through all axes while logging any non-finite matrix

## 4. Shadow Storage and Render Graph

- [x] 4.1 Relax the `array_layers == 1` assertion in `SceneDataManager::SetLightShadowMap` and make the 16x16 fallback shadow map a 4-layer array texture so every bound view has the same type. Verify with a frame rendered with zero shadow-casting lights (fallback bound, no validation error)
- [x] 4.2 Add the shared shadow-resource construction helper (request one 4-layer 2048² D32 array texture per shadow-casting light present, build one pass per light that loops its cascades with per-layer attachment ranges, register the textures with `SceneDataManager`). Verify it is referenced by both `ComplexRenderGraphBuilder` and `EditorRenderGraphBuilder` and that no shadow-pass construction remains duplicated
- [x] 4.3 Wire the shared helper into `ComplexRenderGraphBuilder::BuildDefaultRenderGraph` and the editor's `EditorRenderGraphBuilder`. Verify both applications build and render the test scene with shadows (requires build)
- [x] 4.4 Clear all shadow-map slots before re-registering them, using the existing `RemoveLightShadowMap`, so a rebuilt graph cannot leave dangling pointers. Verify by rebuilding the render graph mid-run and confirming no validation error, crash, or shadow sampling from destroyed storage
- [x] 4.5 Tune `builtin_assets/material_templates/ShadowMapTemplate.asset` rasterizer bias to slope ≈ 1.5 and constant ≈ 0 now that the shader no longer applies a fixed depth bias. Verify no shadow acne appears on the test scene's ground plane

## 5. Lit Shading

- [x] 5.1 Confirm `builtin_assets/shaders/shadowmap.vert.0.glsl` needs no body change (the engine now passes `light * 4 + cascade` through the existing index) and update its comment to say so. Verify the shadow map contains the expected geometry for each cascade (visual, per-layer inspection or by disabling cascades one at a time)
- [x] 5.2 In `lambertian_cook_torrance.frag.0.glsl`, implement cascade selection by view-space depth against `cascade_splits`, delete the fixed `SHADOW_BIAS`, apply the per-cascade normal offset along the geometric normal (texel world size derived from the orthographic matrix), and sample through `sampler2DArrayShadow`. Verify the ground shows no acne and objects show contact shadows
- [x] 5.3 Add the 16-tap Poisson PCF kernel with per-pixel interleaved-gradient-noise rotation at 1.25 texel radius. Verify shadow edges are visibly soft rather than single-texel hard steps
- [x] 5.4 Add the 10% cascade transition blend (smoothstep, sampling both cascades in the band) and the fade over the final 20% of the shadow distance. Verify no seam line where cascades meet and no visible pop as geometry crosses the shadow distance
- [x] 5.5 Mirror the shadow changes into `blinn_phong.frag.0.glsl` so the Blinn-Phong lit path matches. Verify the shadow map test (which uses the Blinn-Phong template) renders identical shadow behaviour
- [x] 5.6 Leave the legacy `shader/` directory untouched and note in the change summary that it remains out of sync with the engine. Verify by confirming no file under `shader/` is modified

## 6. Test Scene and Acceptance

- [x] 6.1 Rewrite `test/engine/windowed/shadow_map_test.cpp` as the large-scale CSM scene: a 200x200 ground plane, objects at roughly 1 / 5 / 20 / 60 / 120 world units from the camera path, one object hovering 0.05 units above the ground, one renderer explicitly opted out of shadow casting, a light slowly rotating through the straight-down orientation, an orbiting camera, and geometry beyond the shadow distance. Verify the scene builds and renders (requires build)
- [x] 6.2 Raise the test's frame count to 300 in `test/engine/windowed/CMakeLists.txt` (`FRAMES 300`). Verify `ctest -R shadow_map_test` passes and exits 0 (requires build)
- [x] 6.3 Add the in-test assertions: every cascade matrix is finite each frame, translating the light GameObject leaves the uploaded cascade matrices unchanged, and the run produces no validation-layer errors. Verify the assertions fail when deliberately broken (e.g. reintroduce the degenerate up vector) and pass otherwise (requires build)
- [x] 6.4 Prepare the visual acceptance checklist for review: light position invariance, straight-down light, no shadow swimming under camera motion, contact shadow on the hovering object, decreasing cascade density with no seam, and no dynamic shadows beyond the shadow distance

## 7. Renderer Shadow-Cast Flag (Independent)

- [x] 7.1 Pass `FilterCriteria{FilterCriteria::BinaryCriterion::Yes}` for the shadow-caster criterion in the shared shadow pass construction so only opted-in renderers are drawn into shadow maps. Verify an opted-out renderer casts no shadow while its lit appearance and other renderers' shadows are unchanged
- [x] 7.2 Flip `RendererComponent::m_cast_shadow` to default `true` so newly created renderers and programmatic scenes keep their current appearance. Verify a programmatically created `StaticMeshComponent` casts shadows without setting the flag
- [x] 7.3 Set the three stored `false` values in `projects/test_project/assets/default_level.asset` (two occurrences) and `projects/test_project/assets/GO_red_brick_sphere_512.asset` (one) to `true` so the sample project keeps its current look. Verify the sample project renders shadows as before (requires build)
- [x] 7.4 Record the migration note for content authored while the flag was inert, and add the deliberately opted-out renderer from task 6.1 as standing evidence. Verify the note exists in the change summary and the opted-out object is present in the test scene

## 8. Wrap-Up

- [x] 8.1 Run the full test suite (`ctest --preset msvc-debug`) and confirm no regressions beyond the expected shadow appearance changes (requires build)
- [x] 8.2 Update in-code documentation: remove the obsolete shadow TODO in `SceneDataManager`, document the cascade fit inputs, and note the `m_layer`/`m_priority` filter fields remain inert. Verify by grepping for the removed TODO and reading the new comments
