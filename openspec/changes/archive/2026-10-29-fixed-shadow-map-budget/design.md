## Context

See `proposal.md` - Why for the motivation. The state that shapes this design:

- Shadow storage is requested at render-graph **build** time from `WorldSystem::GetShadowCastingLightCount()` (`engine/Framework/Tools/ShadowCascadeResources.cpp:27-49`), and the graph is built once (`openspec/specs/editor-physics-pipeline`, "The graph is built once") and merely re-recorded afterwards. `MainClass::LoadProject`'s `FlushCmdQueue()` (`engine/Framework/MainClass.cpp:112`) exists only so the level's lights exist before the graph is built.
- Memory: one shadow map = 2048² × D32 × `MAX_SHADOW_CASCADES` (4) layers = 64 MB. With `MAX_SHADOW_CASTING_LIGHTS = 4` the worst case is 256 MB, which is why the `cascaded-directional-shadows` change (Decision 7) rejected always allocating the maximum and chose to allocate "for the lights that actually exist at graph build time".
- The runtime already has every degradation path a fixed budget needs: passes return early for empty slots (`ShadowCascadeResources.cpp:76-83`), descriptors bind `default_light_map` for slots without storage (`SceneDataManager.cpp:531`), and the only count-dependent code is "how many textures to request and how many passes to add".
- `RenderGraphBuilder::ImportExternalResource(RenderTargetTexture&, prev_access)` already exists (`RenderGraphBuilder.h:42-45`). Its documented purpose is exactly cross-frame persistent textures (the TAA case), and the path is exercised by `mrt_test`, `new_material_test`, `headless_offscreen_test` and others. The `model_matrices` work in `stable-buffer-identity` (D12) is the same shape of precedent: render-side ownership plus unconditional graph import, which deleted a whole rebuild path.
- Graph rebuilds are a real path: PhysicsApp rebuilds its graph on every `CommitScene` (`app/physics/PhysicsApp.cpp:784-787`), and today that requires re-registering shadow maps afterwards (the clear-first discipline of `cascaded-directional-shadows` Decision 12).
- `SceneDataManager::Create()` already owns and initializes `default_light_map` (`SceneDataManager.cpp:226-241`), so render-side ownership of a depth texture that is written into descriptors is an established pattern.

## Goals / Non-Goals

**Goals:**

- Shadow storage allocation and availability depend only on a compile-time budget - not on scene state, the command queue, or when the graph is built.
- A single shadow-casting directional light, with role semantics that are deterministic in the engine and explicit in the editor.
- Delete the `LoadProject` flush workaround and the entire "re-register after a rebuild" protocol.

**Non-Goals:**

- No change to cascade fitting, PCF, bias or other image-quality behaviour; `SHADOWMAP_SIZE = 2048` and `MAX_SHADOW_CASCADES = 4` stay.
- No runtime quality settings (configurable shadow resolution or cascade count). This design assumes compile-time constants; if such settings are introduced later, use the `EnsureModelMatricesCapacity`-style in-place replacement or a rebuild plus re-import (see Risks).
- No point/spot light shadows (a separate cube map budget).
- No change to the non-shadow light slots (`MAX_NON_SHADOW_CASTING_LIGHTS`) or to the legacy `shader/` directory.

## Decisions

### Decision 1: Shadow storage is a fixed, render-owned budget that graphs only borrow

**Chosen**: `SceneDataManager::Create()` allocates every shadow map the budget allows (one `D32SFLOAT` array texture per slot, `MAX_SHADOW_CASCADES` layers, description derived from the same constants); `AddShadowCascadePasses` imports each slot and builds its pass; descriptors are written directly from the manager's own members, and empty slots keep using `default_light_map`.

**Alternatives considered**:
- Graph-owned storage with a fixed budget (workable, but it keeps the cross-subsystem raw pointers in `bound_shadow_maps` and the "always re-register after a rebuild" manual invalidation protocol - the only place in the engine with that shape, and `model_matrices` already paid for it once);
- Lazy allocation on demand (impossible: the graph owns transient textures and is built once);
- Keep count-based allocation and rebuild the graph on change (see the proposal: against the build-once philosophy and causes editor hitches).

**Rationale**: The ownership direction matches `default_light_map` and `model_matrices`; the dangling-reference bug class becomes impossible by construction instead of guarded by discipline; `SetLightShadowMap` / `RemoveLightShadowMap` / `bound_shadow_maps` and `RegisterShadowCascadeMaps` are net deletions.

**Cross-frame layout**: import with `prev_access = None`. The graph treats a resource's first use as transitioning from `eUndefined` (`RenderGraphBuilder.cpp:551-554`), so each frame's shadow-pass entry barrier discards the contents - which is semantically safe here, because every frame either clears and redraws all cascades before they are sampled, or the slot has no owner (the descriptor samples `default_light_map`). The alternative, `prev_access = ShaderSampledRead` plus an initial clear (`default_light_map` style), only buys a more "correct" layout story at the cost of requiring imports before every `AddPass` plus an extra clear, so it is not taken.

### Decision 2: Budget = allocation = 1

**Chosen**: `MAX_SHADOW_CASTING_LIGHTS` 4 → 1, synchronized with `builtin_assets/shaders/include/engine/interface.glsl` (the uniform-block arrays and the descriptor array shrink to one slot accordingly). The allocation size is derived from the constant, never hard-coded to 1.

**Alternatives considered**:
- Pre-allocate all four slots (256 MB resident - already rejected once by `cascaded-directional-shadows`);
- Two slots (insurance for dual-sun art; 128 MB plus a more complex role UX);
- Keep four slots allocated on demand (the status quo, a dead end).

**Rationale**: CSM is the "sun" tool, and a single shadow-casting light is the mainstream model (Unreal's dominant directional light, URP's main light shadow); 64 MB constant is cheap. Because "allocation = budget" rather than "allocation = 1", raising the budget to 2 later is a constant change plus a shader define, not an architectural one.

### Decision 3: The holder is the earliest-created flagged light, independent of brightness

**Chosen**: Every frame, the holder is the flagged directional light that was created first (lowest component creation order); every other flagged light is reported once with a warning and demoted to unshadowed lighting (the same degradation today's over-budget lights get). `WorldSystem::UpdateLightData`'s brightness sort is retired.

**Alternatives considered**:
- Brightest-first (the status quo; rejected because editing intensity or colour would silently steal the shadows, making ownership drift invisibly);
- Hierarchy order (changes on reparenting);
- A scene-level role handle (a new serialization surface, when the per-light flag is already serialized).

**Rationale**: Creation order is deterministic and immune to art parameters. A conflict is visible instead of surprising: the inspector warns on every losing light (Decision 4) and the engine logs the demotion once, so hand-edited files and programmatic creation degrade the same observable way.

### Decision 4: A permissive engine, and an editor that only edits its own light

**Chosen**: Add a dedicated `LightComponent` inspector (through `ComponentInspectorRegistry`). Each light's Cast Shadow checkbox edits only that light - the editor never clears another light's flag. The role holder (Decision 3) is labelled and shows `m_cascade_count` / `m_shadow_distance`; a **flagged** light that does not hold the role shows a warning that it will not cast shadows and names the holder; an unflagged light shows a notice naming the holder (or "no light casts shadows"). Both fields are read-only on non-holders, with their values preserved.

**Alternatives considered**:
- Transfer semantics (clearing the previous holder's flag in the same edit): the earlier choice; rejected by the product decision because one light's checkbox silently rewriting another light's serialized field is surprising and leaves no trace of what changed;
- A scene-level "Shadow Light" dropdown (new UI and serialization surface);
- Resolution with warnings only in the log: the inspector warning keeps the conflict visible where the user is looking.

**Rationale**: Editing one field must not mutate a different component. The conflict stays explicit - the inspector warns on the losing light and the engine logs the demotion once - and the user resolves it by clearing the flag they no longer want. Resolution stays deterministic (creation order) either way.

### Decision 5: Shrink the interface and keep the index formula

**Chosen**: `MAX_SHADOW_CASTING_LIGHTS` in `interface.glsl` becomes 1, so `light_vp_matrix` and the other per-light arrays shrink to `MAX_SHADOW_CASCADES`. The flattened `light * MAX_SHADOW_CASCADES + cascade` index formula stays (`light` is always 0), and the shadowmap.vert push-constant protocol is untouched.

**Alternatives considered**: Simplify the index to just `cascade` while here - touches the push-constant protocol and several shaders for no benefit.

**Rationale**: Smallest possible shader surface, and `shadow_constants_test` already checks the defines against the C++ constants.

## Risks / Trade-offs

- **[Discard-style cross-frame layout (`eUndefined`)]** → Safe because contents are fully rewritten before sampling; but one **pre-existing** edge remains: on a light's first frame, when `cascade_count == 0`, the pass returns early without even clearing, so the lit pass samples undefined contents for that frame (identical before and after this change). Mitigation: clear the slot's layer set before the early return (a small hardening task in `tasks.md`).
- **[A single shadow caster is a real product restriction]** → Mitigation: the budget is one constant plus a shader define, the cost of raising it to 2 is quantified (128 MB) and needs no architectural change; the editor role UX is designed for exactly one role and does not reserve complexity for two slots.
- **[Multiple flags are a normal editor state]** → Nothing is silently changed: the engine logs the demotion once per losing light and the losing light's inspector warns, so the user clears the flag explicitly.
- **[The creation-order winner is invisible outside the editor]** → Mitigation: the engine warning names the losing light and the inspector warning names the holder.
- **[Every test that creates a RenderSystem pays 64 MB]** → Same pattern as `default_light_map`; negligible.
- **[Future quality settings would need to rebuild storage]** → Does not exist under compile-time constants; if resolution or cascade-count settings are introduced, use in-place replacement (stable object, replaced handle) or rebuild plus re-import, and re-argue `ImportExternalResource`'s `prev_access` ("undefined on the first frame, read-only afterwards"). Recorded as a constraint, out of scope here.

## Migration Plan

1. Land the engine constants, `interface.glsl`, the uniform-block and descriptor layout, and both lit shaders in one commit (following the `cascaded-directional-shadows` rule: no intermediate commit may render against a mismatched interface).
2. No serialized-content migration: the `m_cast_shadow` / `m_cascade_count` / `m_shadow_distance` fields and defaults are unchanged. Scenes with several flags are handled by creation order plus the warning, and can be converged to a single holder in the editor.
3. Rollback: revert the whole change as a unit (constants, shaders, engine, editor, tests).
