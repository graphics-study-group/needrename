## Context

See `proposal.md` — Why for the motivation. The current state that shapes this design:

- The directional shadow matrix is produced by `SceneDataManager::SetLightDirectional` (`engine/Render/RenderSystem/SceneDataManager.cpp:253-265`) with a hard-coded `ortho(-20, 20, -20, 20, 0.001, 100)` and `lookAtRH(-dir * 30, origin, up = (0,1,0))`.
- The light uniform buffer holds exactly one `mat4` per light (`SceneDataManager.cpp:25-37`); binding 1 is a fixed 8-slot `eCombinedImageSampler` array with immutable comparison samplers (`SceneDataManager.cpp:56-72`, `:104-119`).
- The shadow pass draws the whole renderer list into a full-extent 2048² depth target, one pass per light slot, gated at runtime by `i < GetNumShadowCastingLights()` (`engine/Framework/Tools/ComplexRenderGraphBuilder.cpp:77-140`). The light index is smuggled to the vertex shader through the `camera_id` push constant (`builtin_assets/shaders/shadowmap.vert.0.glsl:14`).
- The render graph is built **once** at scene commit (`MainClass::SetRenderGraph`, called from `app/physics/PhysicsApp.cpp:784-789` and the editor example) and merely re-recorded every frame, so transient textures are allocated once per build and their number cannot track the per-frame light count.
- A second, near-identical shadow pass structure exists in the editor graph (`app/editor/EditorRenderGraphBuilder.cpp:89,134,342`).
- Investigation results that make the chosen approach safe: `array_layers` reaches `VkImageCreateInfo` unchanged (`engine/Rhi/Texture/Texture.cpp:163-180`, `engine/Rhi/Device/AllocatorState.cpp:351`); a subresource range of one array layer yields a valid per-layer 2D depth view that is accepted directly as a dynamic-rendering attachment (`Texture.cpp:23-33,47-48,219-227`; `engine/Render/AttachmentUtilsFunc.h:45-61`); the render graph emits one whole-image barrier per resource per pass with range `RemainingArrayLayers` (`engine/Render/Pipeline/RenderGraph/RenderGraphBuilder.cpp:615-617`), so several nested `BeginRendering` calls writing different layers need no extra transitions.
- The descriptor set layout needs no change for array sampling: binding 1 is already `eCombinedImageSampler × N` and the reflection layer does not validate arrayness (`engine/Rhi/Pipeline/InterfaceBindingResolver.cpp:11-17`).

## Goals / Non-Goals

**Goals:**
- Directional shadows whose coverage and quality follow the camera, independent of the light object's position, valid for every light orientation.
- No hand-tuned resolution or bias: constants and per-cascade derived values only, with two user-facing fields on the light.
- Soft edges, no cascade seams, shadows attached to their casters.
- Keep point/spot light behaviour, the RHI, and the existing pipeline/material-template machinery untouched.
- A single shared shadow-resource construction path used by both the main and editor render graphs.

**Non-Goals:**
- PCSS / contact-hardening soft shadows, and screen-space contact shadows.
- Per-cascade caster culling (v1 draws the full caster list into every cascade).
- Atlas packing (the array-layer approach is the cheaper path in this render graph).
- Virtual shadow maps or any page-based shadow storage.
- Shadow casting for point/spot lights.
- Per-camera cascade sets (split screen, editor viewport matrices).
- Cleanup of the stale legacy `shader/` directory.
- A headless CI variant of the shadow test.

## Decisions

### Decision 1: Cascades fitted to slices of the active camera frustum

**Chosen**: Each shadow-casting directional light keeps four cascade entries. Every frame the camera-facing fit takes the eight corners of the camera sub-frustum for a slice, transforms them into light space, and builds a tight orthographic projection from the resulting AABB. Slice boundaries use the practical split scheme `split_i = mix(near·(far/near)^(i/N), near + (far−near)·i/N, λ)` with `λ = 0.8`, and cascade 0's near plane is clamped to `max(camera_near, shadow_distance × 0.001)` so the engine's `1e-3` camera near plane cannot waste a whole cascade.

**Alternatives considered**: uniform splits (poor near-field density), pure logarithmic splits (unstable when the near/far ratio is extreme), fixed percentages (Godot's 10/20/50 — predictable but does not adapt to the configured shadow distance), and "stable fit" against the slice's bounding sphere (more temporally stable, but noticeably lower texel density).

**Rationale**: The practical split scheme adapts to any camera near plane and shadow distance, and a tight fit plus texel snapping gives the best density per cascade. The light's world position never enters the computation, which is what makes "drop the light anywhere" true.

### Decision 2: One 2D-array depth texture per light, one pass that loops the cascades

**Chosen**: A shadow-casting light owns a `D32SFLOAT` texture with `array_layers = 4` and 2048² per layer, sampled as `sampler2DArrayShadow`. One render-graph pass per light slot renders all of its cascades: it computes the caster list once, then for each cascade calls `BeginRendering`/`EndRendering` with the per-layer attachment range `{cascade, 1, 0, 1}`.

**Alternatives considered**:
- **One pass per cascade**: the render graph emits a full-image WAW barrier per pass, so four passes cost four whole-image depth barriers per light instead of one.
- **Atlas (tiles in one 2D texture)**: keeps single-layer attachments, but `CommandBuffer::SetupViewport` hardcodes `x = y = 0` (`engine/Render/Pipeline/CommandBuffer.cpp:288-296`) and `DrawRenderers` re-sets the viewport from its own extent (`:359-360`), so offsets would have to be threaded through both — plus UV math and PCF-vs-tile-edge padding.
- **Four separate textures per light**: no layered attachment at all, but multiplies descriptor slots and re-introduces per-cascade registration bookkeeping.

**Rationale**: Layered attachment ranges, per-layer views, and single-barrier transitions are all already supported by the existing machinery; the pass count per light stays at one.

### Decision 3: Flattened cascade matrices and reused light index

**Chosen**: `light_vp_matrix` becomes `mat4[16]` indexed `light * 4 + cascade`. The shadow pass keeps passing its light slot through the existing `camera_id` push constant, now as `light * 4 + cascade`, so `DrawRenderers`' call protocol and the vertex shader body are unchanged. A per-light `vec4 cascade_splits[4]` plus the cascade count travels in the light uniform block; the per-cascade texel world size is derived in the shader from the orthographic matrix (`2 / (vp[0][0] × shadowmap_size)`) instead of being uploaded.

**Alternatives considered**: a two-dimensional `mat4[4][4]` plus explicit light/cascade push constants (touches the push-constant struct, every shader, and every draw call), and uploading per-cascade texel sizes (a second source of truth for a value the matrix already encodes).

**Rationale**: Smallest possible blast radius on the shader interface while keeping one canonical source for derived values.

### Decision 4: Normal offset replaces the fixed depth bias

**Chosen**: Delete the shader's fixed `SHADOW_BIAS = 0.005`. Offset the receiver's world position along its **geometric** normal by `1.5 × cascade texel world size` before projecting into light space, and keep the material template's slope-scaled rasterizer bias as a fallback (slope ≈ 1.5, constant ≈ 0).

**Alternatives considered**: raising the constant bias (this is precisely what produces detached, floating shadows — Unity's and Unreal's own documentation describe the symptom), slope-scaled bias alone (still a depth-space offset), and front-face culling in the shadow pass (effective on watertight meshes, but this engine's built-in meshes include open planes, which would vanish from shadow maps entirely).

**Rationale**: A normal offset moves the sampling point, not the depth comparison, so contact shadows survive; scaling it by the cascade's texel size keeps the correction proportional to the sampling error instead of to a 100-unit depth range.

### Decision 5: PCF, cascade blending and distance fade as fixed internal constants

**Chosen**: 16-tap Poisson kernel with a per-pixel interleaved-gradient-noise rotation at 1.25 texel radius; adjacent cascades blended with `smoothstep` across a band equal to 10% of the cascade's depth interval; shadows faded out over the final 20% of the shadow distance.

**Alternatives considered**: a 2×2 implicit hardware PCF (today's behaviour — hard edges), a separable screen-space blur of the shadow term (stable but uniformly soft, no better contact), and PCSS (deferred — see Non-Goals).

**Rationale**: 10% is Unreal's default cascade transition fraction, and Godot's default `fade_start` of 0.8 matches the 20% fade. All three are cheap, stable, and deliberately not exposed as knobs.

### Decision 6: Cascade depth range extends toward the light by the cascade's own width

**Chosen**: A cascade's orthographic near plane is pushed toward the light by the cascade box's world width, and the lateral extents are grown by 5%.

**Alternatives considered**: a fixed world-space offset (Godot's `pancake_size = 20` — scale-dependent), and fitting the range to the scene's bounds (correct, but this engine has no mesh AABB or scene-bounds infrastructure at all, so it would be new infrastructure for a marginal gain).

**Rationale**: "As deep as it is wide" scales automatically with cascade size — the deeper a cascade reaches, the more offset a caster can have — and needs no new data. The 5% lateral margin reduces the same class of artefact for casters just outside a cascade's cross-section.

### Decision 7: Four shadow-casting lights, selected brightest-first, allocated at graph build time

**Chosen**: `MAX_SHADOW_CASTING_LIGHTS` becomes 4 in both the engine and `engine/interface.glsl`. Per frame, shadow-casting directional lights are sorted by brightness (colour × intensity) and the brightest four keep shadow slots; the rest log a warning and light the scene unshadowed. Storage is requested for the shadow-casting lights that exist when the render graph is built.

**Alternatives considered**: keeping 8 interface slots and capping effectively (rejected — the interface constant should state the real budget), always allocating four 4-layer textures (a constant 256 MB even for a single sun), and lazily allocating on demand (impossible: the graph owns transient textures and is built once).

**Rationale**: One 4-layer 2048² colour-depth array is 64 MB, so the common single-sun scene costs 64 MB and the worst case is bounded at 256 MB. Making the budget explicit in the interface keeps the shader, the descriptor array, and the uniform block consistent.

### Decision 8: Stable light basis and XY-only texel snapping

**Chosen**: Build the light basis from the light direction and world Y, switching to world Z when `|dot(direction, Y)| > 0.99`. Snap the orthographic extents to whole texels in light-space X/Y only; leave Z unsnapped.

**Alternatives considered**: deriving the basis up-vector from the camera (it would rotate the snapping grid with the camera and reintroduce exactly the swimming this change removes), and snapping Z as well (makes casters pop as they cross the range boundary).

**Rationale**: A world-fixed reference axis is what makes the snapping grid stable in world space; the fallback removes the NaN degeneracy that the current `up = (0,1,0)` produces for a midday sun.

### Decision 9: Fit logic lives in `SceneDataManager`, verification is a frame-running program

**Chosen**: The cascade fit is written directly in `SceneDataManager` as file-local helpers, with no GPU-independent abstraction layer and no unit tests. Verification is the upgraded windowed test scene, which asserts finite cascade matrices, light-position invariance, and a clean validation layer over 300 frames; remaining acceptance is visual review.

**Alternatives considered**: a POD-in/POD-out helper class with unit tests (rejected by the project owner: it would duplicate data structures that already exist on the GPU side without adding value), and computing the fit in `WorldSystem` (leaks render math into the framework layer).

**Rationale**: The matrices must be produced on the CPU next to the uniform block they fill; keeping the math there avoids a second representation of the same data.

### Decision 10: Both render graphs share one shadow-resource construction path

**Chosen**: Extract the shadow-resource construction (request the per-light array textures, build the per-light cascade passes, register the textures with `SceneDataManager`) into one shared helper used by `ComplexRenderGraphBuilder` and `EditorRenderGraphBuilder`.

**Alternatives considered**: updating both builders independently (they are already near-duplicates, and the shader-interface change means both must change together).

**Rationale**: This is deduplication of pass construction, not a second copy of GPU data; leaving two copies guarantees drift.

### Decision 11: The renderer shadow-cast flag becomes effective in its own task group

**Chosen**: The shadow pass requests `FilterCriteria{is_shadow_caster = Yes}`, and `RendererComponent::m_cast_shadow` flips to default `true`. The sample project's three stored `false` values are set to `true` to preserve their current appearance, and the upgraded test scene includes one renderer explicitly opted out as evidence the flag works.

**Alternatives considered**: leaving the flag inert (documented as a known issue and handled later — rejected by the project owner as a cheap fix worth taking now), and flipping the filter without flipping the default (would silently remove all shadows, since the flag is serialized as `false` everywhere today).

**Rationale**: The change is a filter argument plus a default; keeping it in its own task group keeps the CSM work reviewable in isolation.

### Decision 12: Shadow map references are invalidated on graph rebuild

**Chosen**: Shadow-map registration clears all slots before re-registering, using the existing `RemoveLightShadowMap` API that currently has zero call sites.

**Alternatives considered**: leaving the dangling-pointer path alone (it is undefined behaviour on the very first frame after any graph rebuild — a scene commit or an editor rebuild) and giving `SceneDataManager` shared ownership of graph-owned textures (wrong direction of ownership).

**Rationale**: This change rewrites exactly this registration path for a variable number of lights, so the fix belongs with it.

## Risks / Trade-offs

- **[Layered depth attachment verified only by inspection]** → The RHI path was traced end to end but no driver has been exercised yet. Mitigation: the fallback is to give each cascade its own texture slot, which changes only the shader/decriptor internals, not the specs, the fit, or the pass structure; the shared construction helper (Decision 10) is the single place that would change.
- **[Shadow pass cost multiplies by cascade count]** → With no per-cascade culling, casters are drawn four times per light. Mitigation: accepted for v1 (the shadow pass is one draw list per light, the renderer list is computed once per light, and per-cascade culling is listed as a follow-up; it needs mesh bounds infrastructure that does not exist yet).
- **[64 MB per shadow-casting light, up to 256 MB]** → Mitigation: the four-light budget, brightest-first selection, and build-time allocation for lights that actually exist.
- **[Lights added after graph construction get no storage]** → Mitigation: an explicit warning and unshadowed lighting rather than sampling unallocated storage; documented in the spec as a scenario.
- **[Honoring `m_cast_shadow` changes the appearance of existing content]** → Mitigation: default flips to `true`, the two sample-project files are updated, and the behaviour change is called out as breaking in the proposal with a migration note.
- **[Bias, PCF radius and fade constants may need tuning after eyes-on review]** → Mitigation: they are single internal constants in one place, and the acceptance criteria are visual.
- **[Casters just outside a cascade's cross-section can still be clipped]** → Mitigation: the 5% lateral margin reduces it; eliminating it entirely needs a per-cascade extended fit or caster culling, both out of scope.

## Migration Plan

1. Land the shader interface change and both lit shaders together with the engine-side uniform layout, so no intermediate commit renders against a mismatched interface.
2. Recompile all shaders during the build; any project shader that includes `engine/interface.glsl` must switch to the array shadow sampler and the flattened cascade matrix array.
3. Update serialized content: the sample projects' three stored `m_cast_shadow = false` values (two files) are set to `true`; other projects must review the flag because it is now effective.
4. Scenes with more than four shadow-casting directional lights lose shadows on the extras (warning logged).
5. Rollback: revert the change as a unit (shaders, engine layout, sample content). The two new light fields are additive; no existing serialized field changes meaning.

## Open Questions

- The exact PCF radius, normal-offset scale and slope-bias values are expected to need one round of eyes-on tuning after the first build; adjusting them changes no spec, no approach, and no task structure.
