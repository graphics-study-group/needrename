#version 450
#extension GL_ARB_shading_language_include : require
#include <engine/interface.glsl>

layout(constant_id = 0) const int SPECULAR_SHADING_MODE = 2;

/// Number of shadow comparison taps per cascade sample.
#define SHADOW_PCF_TAPS 16
/// Radius of the PCF kernel in shadow texels.
#define SHADOW_PCF_RADIUS 1.25
/// Distance the sampling point is moved along the geometric normal, in shadow texels.
#define SHADOW_NORMAL_OFFSET 1.5
/// Fraction of a cascade's depth interval blended into the next cascade.
#define SHADOW_CASCADE_TRANSITION 0.1
/// Fraction of the shadow distance at which the shadow starts fading out.
#define SHADOW_FADE_START 0.8
#define TWO_PI 6.28318530718

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in vec3 frag_position;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 1) uniform sampler2D base_tex;
layout(set = 2, binding = 0) uniform Material {
    vec4 specular_color;
    vec4 ambient_color;
} material;

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
    // The orthographic matrix encodes the cascade's world width in its first row.
    const float texel_world_size = 2.0 / (light_vp[0][0] * SHADOWMAP_SIZE);
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

void main() {
    vec3 frag_position_vs = (camera.cameras[pc.camera_id].view * vec4(frag_position, 1.0)).xyz;
    // Get normalized normal vector in view space
    vec3 normal_vs = normalize(mat3(camera.cameras[pc.camera_id].view) * frag_normal);
    // Get view position in view space
    vec3 view_position_vs = vec3(0.0, 0.0, 0.0);
    // Get view vector
    vec3 view_vs = normalize(view_position_vs - frag_position_vs);

    // Cascade selection works on the view space depth; the normal offset uses the
    // geometric normal.
    const float view_depth = -frag_position_vs.z;
    const vec3 geometric_normal = normalize(frag_normal);
    const float shadow_rotation = interleavedGradientNoise(gl_FragCoord.xy) * TWO_PI;

    // Sample base color
    const vec3 base_color = texture(base_tex, frag_uv).rgb * frag_color;
    const vec3 ambient = material.ambient_color.rgb * base_color;
    
    vec3 totalLight = vec3(0.0);
    float shininess = material.specular_color.w;

    // Process non-casting lights
    for (int i = 0; i < scene.noncasting_light_count; ++i) {
        // Get normalized incident vector pointing from the light source
        vec3 incident_vs = vec3(0.0);
        if (scene.noncasting_lights.light_source[i].w == 0.0) // 0: directional light
            incident_vs = normalize(mat3(camera.cameras[pc.camera_id].view) * scene.noncasting_lights.light_source[i].xyz);
        else if (scene.noncasting_lights.light_source[i].w != 0.0) // 1: point light
            incident_vs = normalize((camera.cameras[pc.camera_id].view * vec4(frag_position - scene.noncasting_lights.light_source[i].xyz, 1.0)).xyz);

        // Calculate diffuse coefficient
        float diffuse_coef = max(0.0, dot(-incident_vs, normal_vs));

        // Calculate specular coefficient
        float specular_coef = 0.0;
        if (SPECULAR_SHADING_MODE == 1) {
            // Phong shading
            vec3 reflected = reflect(incident_vs, normal_vs);
            specular_coef = pow(max(0.0, dot(view_vs, reflected)), shininess);
        } else if (SPECULAR_SHADING_MODE == 2) {
            // Blinn Phong shading
            vec3 halfway = normalize(view_vs - incident_vs);
            specular_coef = pow(max(0.0, dot(normal_vs, halfway)), shininess);
        }

        // Calculate light color
        vec3 light_color = scene.noncasting_lights.light_color[i].rgb;
        vec3 light = diffuse_coef * base_color * light_color + (specular_coef * material.specular_color.rgb) * base_color * light_color;
        totalLight += light;
    }

    // Process casting lights (directional lights)
    for (int i = 0; i < scene.casting_light_count; ++i) {
        // Get normalized incident vector pointing from the light source
        vec3 incident_vs = normalize(mat3(camera.cameras[pc.camera_id].view) * scene.casting_lights.light_source[i].xyz);

        // Calculate diffuse coefficient
        float diffuse_coef = max(0.0, dot(-incident_vs, normal_vs));

        // Calculate specular coefficient
        float specular_coef = 0.0;
        if (SPECULAR_SHADING_MODE == 1) {
            // Phong shading
            vec3 reflected = reflect(incident_vs, normal_vs);
            specular_coef = pow(max(0.0, dot(view_vs, reflected)), shininess);
        } else if (SPECULAR_SHADING_MODE == 2) {
            // Blinn Phong shading
            vec3 halfway = normalize(view_vs - incident_vs);
            specular_coef = pow(max(0.0, dot(normal_vs, halfway)), shininess);
        }

        // Calculate the shadow factor with cascade selection and PCF
        float shadow_coef = calculateShadow(i, frag_position, geometric_normal, view_depth, shadow_rotation);

        // Calculate light color
        vec3 light_color = scene.casting_lights.light_color[i].rgb;
        vec3 light = diffuse_coef * base_color * light_color + (specular_coef * material.specular_color.rgb) * base_color * light_color;
        totalLight += shadow_coef * light;
    }

    outColor = vec4(ambient + totalLight, 1.0);
}
