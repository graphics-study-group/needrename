## Purpose

Gives directional lights cascaded shadow maps whose frusta follow the active camera, so shadow coverage, resolution and bias no longer depend on where the light object sits or on where geometry sits relative to the world origin.

## ADDED Requirements

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

At most four directional lights SHALL cast shadows in a frame. When more shadow-casting directional lights are present, the brightest SHALL be selected and the others SHALL be reported with a warning. Shadow map storage SHALL be allocated per shadow-casting light present when the render graph is built.

#### Scenario: More lights than the budget
- **WHEN** a scene contains more than four shadow-casting directional lights
- **THEN** exactly four (the brightest) cast shadows, the remaining lights still light the scene without shadows, and a warning is logged

#### Scenario: Light added after graph construction
- **WHEN** a shadow-casting directional light is added to the scene after the render graph was built
- **THEN** the engine logs a warning and renders that light unshadowed rather than sampling storage that was never allocated

#### Scenario: Exactly at the budget
- **WHEN** a scene contains exactly four shadow-casting directional lights
- **THEN** all four cast shadows, with no assertion failure or out-of-range write

### Requirement: Light component configuration fields

`LightComponent` SHALL carry serialized `m_shadow_distance` (default 100) and `m_cascade_count` (default 4) fields. Lights that do not cast shadows and lights that are not directional SHALL keep their existing behaviour.

#### Scenario: Defaults round-trip through serialization
- **WHEN** a light component is serialized and loaded without explicit shadow fields
- **THEN** it renders with a shadow distance of 100 and four cascades

#### Scenario: Point and spot lights are unaffected
- **WHEN** a scene contains point or spot lights
- **THEN** they light the scene as before and remain non-shadow-casting

### Requirement: Shadow map lifetime across render-graph rebuilds

Shadow map references held by the scene's light data SHALL be invalidated when the render graph that owns them is rebuilt, and the shadow-casting light count SHALL be handled safely at the supported maximum.

#### Scenario: Render graph rebuilt
- **WHEN** the render graph is destroyed and rebuilt (for example on scene commit) and frames continue to render
- **THEN** no frame samples destroyed shadow map storage and no validation error or crash occurs

#### Scenario: Shadow storage released with the graph
- **WHEN** a rebuilt graph allocates new shadow map storage
- **THEN** the light data references only the newly allocated storage for subsequent frames
