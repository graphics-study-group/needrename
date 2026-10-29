## Why

Cascaded shadow map storage is sized at render-graph build time from the number of shadow-casting lights that exist in the scene at that moment. That forced a `FlushCmdQueue()` into `MainClass::LoadProject` whose only purpose is to make the level's lights exist before the graph is built, and it does not hold up in the editor anyway: a light added after construction never gets storage (it silently renders without shadows), and a light removed leaves its storage reserved. The shadow budget is already a compile-time constant (2048² × 4 layers of D32 = 64 MB per slot), so the right shape is a fixed budget with runtime gating, not a build-time scene count.

## What Changes

- **BREAKING** `MAX_SHADOW_CASTING_LIGHTS` 4 → 1: at most one directional light casts cascaded shadows at any time; other directional lights still light the scene but cast no dynamic shadows (the same degradation today's over-budget lights already get).
- **BREAKING** Shadow map storage becomes a fixed, render-owned budget: `SceneDataManager::Create()` allocates it once and render graphs only borrow it (`ImportExternalResource`). `SetLightShadowMap` / `RemoveLightShadowMap` and the whole "re-register after a graph rebuild" protocol are deleted, so graph rebuilds (for example PhysicsApp's `CommitScene`) no longer touch shadow storage at all.
- Render graph construction no longer queries the scene's light count: `AddShadowCascadePasses` builds a fixed number of passes, `WorldSystem::GetShadowCastingLightCount` is deleted, and `MainClass::LoadProject` loses its `FlushCmdQueue()` workaround.
- The shadow role (`LightComponent::m_cast_shadow`) is resolved deterministically when several flags coexist (creation order, with a warning); the editor never edits another light's flag, the inspector labels the holder, warns on flagged lights that lose the role, and keeps `m_cascade_count` / `m_shadow_distance` editable only on the holder.
- `MAX_SHADOW_CASCADES` (per-light cascade cap of 4) is unchanged, and the `m_shadow_distance` / `m_cascade_count` fields and their defaults are unchanged.

## Capabilities

### New Capabilities

- `editor-shadow-light-role`: the editor's UX for the unique shadow role - no edits across lights, holder/non-holder display, the warning for a flagged light that loses the role, and visibility rules for the shadow settings.

### Modified Capabilities

- `directional-shadow-cascades`: "Shadow-casting light budget" is rewritten (4 → 1, role selection, adding/removing lights no longer affects availability); "Shadow map lifetime across render-graph rebuilds" is replaced by a render-owned fixed-budget storage requirement (allocation no longer depends on scene state, so `LoadProject` needs no queue flush).

## Impact

- `engine/Render/RenderSystem/SceneDataManager.{h,cpp}`: `Create()` allocates the fixed storage plus an accessor; `SetLightShadowMap` / `RemoveLightShadowMap` / `bound_shadow_maps` are deleted.
- `engine/Framework/Tools/ShadowCascadeResources.{h,cpp}`: import render-owned storage instead of requesting transient textures; `RegisterShadowCascadeMaps` is deleted.
- `engine/Framework/World/WorldSystem.{h,cpp}`: deterministic holder selection plus warning; `GetShadowCastingLightCount` is deleted.
- `engine/Framework/MainClass.cpp`: the `FlushCmdQueue()` call in `LoadProject` is removed.
- `builtin_assets/shaders/include/engine/interface.glsl`: `MAX_SHADOW_CASTING_LIGHTS` 4 → 1 (checked by `shadow_constants_test`).
- `app/editor/`: the LightComponent shadow-role inspector (holder label, losing-role warning, state display) and the HierarchyWidget light presets.
- Tests: `shadow_map_test` (registration protocol removal, light add/remove scenarios), `project_loading_test`, and other affected cases.
