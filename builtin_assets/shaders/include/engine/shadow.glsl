#ifndef SHADOW_GLSL_INCLUDED
#define SHADOW_GLSL_INCLUDED

/**
 * Directional shadow sampling shared by every lit fragment shader.
 *
 * The functions here read `scene.casting_lights` and `light_shadowmaps` declared
 * in `interface.glsl`, so a shader only has to pass the fragment's world
 * position, its geometric normal, its view space depth and a per pixel PCF
 * rotation to `calculateShadow`.
 *
 * The quality tunables are compile time constants. A shader that wants different
 * values defines them before including this file:
 *
 *     #define SHADOW_PCF_RADIUS 2.0
 *     #include <engine/shadow.glsl>
 */

#include <engine/interface.glsl>

/**
 * Edge length of one cascade's shadow map in texels.
 *
 * Kept in sync with `SceneDataManager::SHADOWMAP_SIZE` by the host, which
 * allocates the shadow maps. Not overridable: a value that disagrees with the
 * allocation silently scales the PCF radius and the normal offset instead of
 * failing, because both are expressed in texels.
 */
#define SHADOWMAP_SIZE 2048.0

/**
 * Number of shadow comparison taps per cascade sample.
 *
 * Not overridable: it also sizes `SHADOW_POISSON_DISK` below, so a different
 * count needs a different sample pattern rather than a different bound.
 */
#define SHADOW_PCF_TAPS 16

/// Radius of the PCF kernel in shadow texels.
#ifndef SHADOW_PCF_RADIUS
#define SHADOW_PCF_RADIUS 1.25
#endif

/// Distance the sampling point is moved along the geometric normal, in shadow texels.
#ifndef SHADOW_NORMAL_OFFSET
#define SHADOW_NORMAL_OFFSET 1.5
#endif

/// Fraction of a cascade's depth interval blended into the next cascade.
#ifndef SHADOW_CASCADE_TRANSITION
#define SHADOW_CASCADE_TRANSITION 0.1
#endif

/// Fraction of the shadow distance at which the shadow starts fading out.
#ifndef SHADOW_FADE_START
#define SHADOW_FADE_START 0.8
#endif

#define TWO_PI 6.28318530718

/**
 * @brief Interleaved gradient noise in [0, 1).
 *
 * Decorrelates the PCF rotation between neighbouring pixels without a texture.
 */
float interleavedGradientNoise(vec2 position)
{
    return fract(52.9829189 * fract(dot(position, vec2(0.06711056, 0.00583715))));
}

vec2 rotateOffset(vec2 offset, float rotation)
{
    float sine = sin(rotation);
    float cosine = cos(rotation);
    return vec2(offset.x * cosine - offset.y * sine, offset.x * sine + offset.y * cosine);
}

/**
 * @brief Sample one cascade of a light's shadow map with a rotated Poisson PCF kernel.
 *
 * The sampling point is moved along the geometric normal by a fixed number of
 * shadow texels, which keeps contact shadows attached without the depth range
 * dependency of a constant clip space bias.
 */
float sampleShadowCascade(
    int lightIndex, int cascade, vec3 world_position, vec3 geometric_normal, float rotation
)
{
    /// Poisson disk of 16 samples in the unit disk, in texel units.
    const vec2 SHADOW_POISSON_DISK[SHADOW_PCF_TAPS] = vec2[SHADOW_PCF_TAPS](
        vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
        vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
        vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
        vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
        vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
        vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
        vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790)
    );

    const mat4 light_vp = scene.casting_lights.light_vp_matrix[lightIndex * MAX_SHADOW_CASCADES + cascade];
    // The orthographic matrix scales light space by 2 / cascade width along its
    // rows; the first row is the light's right axis scaled by that factor, so
    // its length gives the width whatever the light's orientation is.
    const float light_basis_scale = max(length(vec3(light_vp[0][0], light_vp[1][0], light_vp[2][0])), 1e-6);
    const float texel_world_size = 2.0 / (light_basis_scale * SHADOWMAP_SIZE);
    const vec3 offset_position = world_position + geometric_normal * (SHADOW_NORMAL_OFFSET * texel_world_size);

    const vec4 light_space = light_vp * vec4(offset_position, 1.0);
    const vec3 projected = light_space.xyz / light_space.w;
    const vec2 uv = projected.xy * 0.5 + 0.5;
    const float radius = SHADOW_PCF_RADIUS / SHADOWMAP_SIZE;

    float shadow = 0.0;
    for (int i = 0; i < SHADOW_PCF_TAPS; ++i) {
        const vec2 offset = rotateOffset(SHADOW_POISSON_DISK[i], rotation) * radius;
        shadow += texture(light_shadowmaps[lightIndex], vec4(uv + offset, float(cascade), projected.z));
    }
    return shadow / float(SHADOW_PCF_TAPS);
}

/**
 * @brief Shadow factor of a casting light, 1.0 when lit and 0.0 when shadowed.
 *
 * Selects the cascade covering the fragment's view space depth, blends into the
 * next cascade over a transition band and fades the shadow out at the end of the
 * light's shadow distance, so neither cascade boundaries nor the shadow distance
 * produce a visible edge.
 */
float calculateShadow(
    int lightIndex, vec3 world_position, vec3 geometric_normal, float view_depth, float rotation
)
{
    const uint cascade_count = scene.casting_lights.light_cascade_count[lightIndex];
    if (cascade_count == 0u) return 1.0;

    const vec4 splits = scene.casting_lights.cascade_splits[lightIndex];
    const float shadow_distance = splits[cascade_count - 1u];
    if (view_depth >= shadow_distance) return 1.0;

    int cascade = -1;
    for (uint i = 0u; i < cascade_count; ++i) {
        if (view_depth <= splits[i]) {
            cascade = int(i);
            break;
        }
    }
    if (cascade < 0) return 1.0;

    float shadow = sampleShadowCascade(lightIndex, cascade, world_position, geometric_normal, rotation);

    // Blend towards the next cascade across the end of this cascade's interval.
    const float split_end = splits[cascade];
    const float split_start = (cascade == 0) ? 0.0 : splits[cascade - 1];
    if (cascade + 1 < int(cascade_count)) {
        const float band_start = split_end - SHADOW_CASCADE_TRANSITION * (split_end - split_start);
        if (view_depth > band_start) {
            const float blend = smoothstep(band_start, split_end, view_depth);
            shadow = mix(
                shadow,
                sampleShadowCascade(lightIndex, cascade + 1, world_position, geometric_normal, rotation),
                blend
            );
        }
    }

    // Fade the shadow out instead of ending it abruptly at the shadow distance.
    const float fade_start = shadow_distance * SHADOW_FADE_START;
    if (view_depth > fade_start) {
        shadow = mix(shadow, 1.0, smoothstep(fade_start, shadow_distance, view_depth));
    }
    return shadow;
}

#endif // SHADOW_GLSL_INCLUDED
