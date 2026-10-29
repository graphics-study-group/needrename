## MODIFIED Requirements

### Requirement: Shadow-casting light budget

At most one directional light SHALL cast shadows in a frame. A directional light participates when its `m_cast_shadow` flag is set; when several are flagged, exactly one — the earliest created (lowest component creation order) — casts shadows, every other flagged light SHALL be reported with a warning and SHALL light the scene without dynamic shadows, and the winner SHALL NOT depend on the lights' colour or intensity. Unflagged directional lights, point lights and spot lights SHALL keep their existing behaviour.

#### Scenario: More lights than the budget
- **WHEN** a scene contains more than one shadow-casting-flagged directional light (for example a hand-edited scene file)
- **THEN** exactly one (the earliest created) casts shadows, every other flagged light still lights the scene without dynamic shadows, and a warning names the lights that did not get the role

#### Scenario: Exactly one flagged light
- **WHEN** a scene contains exactly one shadow-casting-flagged directional light
- **THEN** that light casts shadows, with no assertion failure or out-of-range write

#### Scenario: The role holder is removed
- **WHEN** the light holding the shadow-caster role is removed from the scene while another flagged directional light exists
- **THEN** the remaining flagged light casts shadows from the next frame, with no storage reallocation and no render-graph rebuild

#### Scenario: Shadow ownership does not follow brightness
- **WHEN** two flagged directional lights exist and the non-holder's colour intensity is raised above the holder's
- **THEN** the holder keeps casting shadows and the non-holder stays reported with a warning

#### Scenario: Point and spot lights are unaffected
- **WHEN** a scene contains point or spot lights
- **THEN** they light the scene as before and remain non-shadow-casting

## ADDED Requirements

### Requirement: Shadow map storage is a fixed render-owned budget

The scene's light data SHALL reference shadow map storage owned by the render system: one shadow map array texture for every supported shadow-casting light slot (each with `MAX_SHADOW_CASCADES` layers) SHALL be allocated once when the render system's scene data is created, and render graphs SHALL only borrow this storage. Storage allocation and shadow availability SHALL NOT depend on the scene's light state or on any command-queue flush, and a render-graph rebuild SHALL NOT invalidate, reallocate or re-register the storage.

#### Scenario: Render graph built before the scene queue is flushed
- **WHEN** a project level is loaded and the render graph is built immediately afterwards, without flushing the scene's command queue
- **THEN** the level's shadow-casting light casts shadows normally on the first rendered frames, and project loading performs no queue flush for the sake of shadow storage

#### Scenario: Render graph rebuilt mid-run
- **WHEN** the render graph is destroyed and rebuilt (for example on scene commit) and frames continue to render
- **THEN** shadow rendering continues against the render system's own storage with no re-registration step, no crash and no validation error

#### Scenario: Lights come and go at runtime
- **WHEN** shadow-casting-flagged directional lights are added to or removed from the scene at any time after the render system is created
- **THEN** shadow casting starts or stops on the next frame for the current role holder, with no storage allocation change and no render-graph rebuild

## REMOVED Requirements

### Requirement: Shadow map lifetime across render-graph rebuilds

**Reason**: Shadow map storage becomes render-owned (see "Shadow map storage is a fixed render-owned budget"), so the manual invalidation protocol it guarded — clearing and re-registering shadow map references whenever the owning graph is rebuilt — no longer exists; a rebuilt graph never owned the storage to begin with.

**Migration**: Callers no longer register graph-owned shadow maps after building or rebuilding a render graph. `SceneDataManager::SetLightShadowMap` and `SceneDataManager::RemoveLightShadowMap` are removed from the API; render graphs borrow the render system's shadow storage instead of allocating their own.
