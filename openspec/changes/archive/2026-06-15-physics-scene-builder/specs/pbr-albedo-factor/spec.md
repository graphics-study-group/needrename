## ADDED Requirements

### Requirement: PBR Material UBO includes albedoFactor
The PBR fragment shader (`lambertian_cook_torrance.frag`) Material uniform block SHALL contain a `vec4 albedoFactor` field that multiplies the sampled albedo texture color. When an existing material does not specify `albedoFactor` in its asset properties, the shader SHALL behave as if `albedoFactor` is `(1.0, 1.0, 1.0, 1.0)` (identity multiplication, preserving backward compatibility).

#### Scenario: Solid color via albedoFactor
- **WHEN** a PBR material has `albedoSampler` pointing to the white solid-color texture and `albedoFactor` set to `(1.0, 0.2, 0.2, 1.0)`
- **THEN** the rendered albedo color is `(1.0, 0.2, 0.2)` (red tint)

#### Scenario: Backward compatibility with existing materials
- **WHEN** a PBR material does not define `albedoFactor` in its asset properties (e.g., existing `red_brick.asset`)
- **THEN** the rendered albedo color is identical to the sampled texture (albedoFactor defaults to identity)

### Requirement: Solid color material assets
The project SHALL ship preset PBR material assets in `builtin_assets/materials/` that combine the shared white texture with distinct `albedoFactor` values, covering at least: red, green, blue, yellow, cyan, magenta, orange, white, and grey. Each asset SHALL use the existing PBR material library (`PBRLibrary` / Lambertian Cook Torrance PBR).

#### Scenario: Multiple solid colors from one texture
- **WHEN** two objects use `solid_color_red.asset` and `solid_color_blue.asset` respectively
- **THEN** both sample the same underlying white texture but render as red and blue due to their different `albedoFactor` values
