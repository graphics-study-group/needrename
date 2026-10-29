# directional-shadow-cascades Specification

## Purpose

Gives directional lights cascaded shadow maps whose frusta follow the active camera, so shadow coverage, resolution and bias no longer depend on where the light object sits or on where geometry sits relative to the world origin.

## Requirements

### Requirement: Cascade frusta follow the active camera

A shadow-casting directional light SHALL project its shadows through cascades fitted to slices of the active camera's view frustum. Coverage SHALL follow the camera, and the light object's world position SHALL NOT influence the rendered shadows.

#### Scenario: Translating the light leaves shadows unchanged

- **WHEN** the directional light's GameObject is translated (rotation unchanged) and a frame is rendered
- **THEN** the rendered shadows are unchanged, including the cascade matrices uploaded for that frame

#### Scenario: Shadowed geometry far from the world origin

- **WHEN** casters and receivers are placed far away from the world origin but inside the camera's view and within the configured shadow distance
- **THEN** they cast and receive shadows exactly as they would near the origin

#### Scenario: No active camera

- **WHEN** the scene has no active camera for a frame
- **THEN** the previous frame's cascade data is retained and no invalid shadow sampling occurs

### Requirement: Cascade configuration and shadow distance

Each shadow-casting directional light SHALL cover a configurable shadow distance measured from the camera, divided into a configurable number of cascades of decreasing texel density. Shadows SHALL fade out over the final portion of that distance instead of ending abruptly.

#### Scenario: Shadow distance bounds dynamic shadows

- **WHEN** a light uses the default shadow distance of 100 world units
- **THEN** geometry within that distance receives dynamic shadows, shadows fade out over the last 20% of the distance, and geometry beyond it receives no dynamic shadow

#### Scenario: Cascade count controls quality and cost

- **WHEN** the cascade count is set to 1, 2, 3 or 4
- **THEN** the light is shadowed using exactly that many cascades, with finer cascades concentrated near the camera, and the change takes effect on the next frame without rebuilding the render graph

#### Scenario: Out-of-range configuration is clamped

- **WHEN** a serialized light specifies a cascade count outside 1..4 or a non-positive shadow distance
- **THEN** the value is clamped to the supported range and rendering continues

### Requirement: All light orientations are valid

Shadow projection SHALL be well-defined for every light orientation, including straight down and straight up, and cascade extents SHALL be quantised to shadow-texel granularity so that shadows do not crawl or swim as the camera moves.

#### Scenario: Light pointing straight down

- **WHEN** a shadow-casting directional light is rotated to point straight down (or straight up)
- **THEN** finite cascade matrices are produced and shadows render normally, with no NaN or disappearing shadow map

#### Scenario: Camera movement does not make shadows swim

- **WHEN** the camera moves continuously while the light stays fixed
- **THEN** shadow edges remain stable relative to the world rather than shifting by sub-texel amounts each frame

### Requirement: Bias preserves contact shadows

The shadow bias SHALL be derived from each cascade's texel size in world units rather than a fixed depth constant, so that shadows stay attached to their casters (no floating objects) while lit surfaces show no self-shadowing artefacts.

#### Scenario: Object resting close to a surface

- **WHEN** an object hovers a small fraction of a world unit above a surface that receives its shadow
- **THEN** a shadow appears in contact with the object, with no visible gap between object and shadow

#### Scenario: No shadow acne on lit surfaces

- **WHEN** a light shines at a grazing or steep angle onto a lit surface that also shadows itself
- **THEN** the surface shows no stripe-like self-shadowing artefacts

### Requirement: Soft shadows and seamless cascades

Shadows SHALL be filtered with a multi-sample percentage-closer kernel and SHALL blend between adjacent cascades across a transition band, so shadow edges are soft and cascade boundaries produce no visible seam.

#### Scenario: Soft shadow edges

- **WHEN** a caster's shadow is viewed against a large receiver
- **THEN** the shadow edge is soft rather than a single-texel hard step

#### Scenario: No cascade seam on a continuous surface

- **WHEN** a single large surface spans two or more cascades
- **THEN** no hard line or density discontinuity is visible where the cascades meet

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

### Requirement: Light component configuration fields

`LightComponent` SHALL carry serialized `m_shadow_distance` (default 100) and `m_cascade_count` (default 4) fields. Lights that do not cast shadows and lights that are not directional SHALL keep their existing behaviour.

#### Scenario: Defaults round-trip through serialization

- **WHEN** a light component is serialized and loaded without explicit shadow fields
- **THEN** it renders with a shadow distance of 100 and four cascades

#### Scenario: Point and spot lights are unaffected

- **WHEN** a scene contains point or spot lights
- **THEN** they light the scene as before and remain non-shadow-casting

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
