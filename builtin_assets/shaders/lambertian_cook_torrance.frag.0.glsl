/*
 * Lambertian diffuse + Cook-Torrance microfacet specular BSDF.
 * Modified from https://github.com/Nadrin/PBR/blob/master/data/shaders/glsl/pbr_fs.glsl.
 */
#version 450 core
#extension GL_ARB_shading_language_include : require
#include <engine/interface.glsl>

#define M_PI 3.1415926538
#define M_EPS (1e-5)

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

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in vec3 frag_position;
layout(location = 4) in vec4 frag_tangent;

layout(location = 0) out vec4 out_color;

layout(std140, set = 2, binding = 0) uniform Material {
    float metalnessFactor;
    float roughnessFactor;
    vec4 emissiveFactor;
    vec4 albedoFactor;
} material;
layout (set=2, binding=1) uniform sampler2D albedoSampler;
layout (set=2, binding=2) uniform sampler2D MRAOSampler;
layout (set=2, binding=3) uniform sampler2D normalSampler;
layout (set=2, binding=4) uniform sampler2D emissiveSampler;

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

vec3 sampleWorldNormal()
{
    vec3 geometric_normal = normalize(frag_normal);
    vec3 tangent = normalize(frag_tangent.xyz);
    vec3 bitangent = normalize(cross(geometric_normal, tangent)) * frag_tangent.w;
    vec3 tangent_normal = texture(normalSampler, frag_uv).xyz * 2.0 - 1.0;

    mat3 TBN = mat3(tangent, bitangent, geometric_normal);
    return normalize(TBN * tangent_normal);
}

// GGX/Towbridge-Reitz normal distribution function.
// Uses Disney's reparametrization of alpha = roughness^2.
float ndfGGX(float cosLh, float roughness)
{
    float alpha   = roughness * roughness;
    float alphaSq = alpha * alpha;

    float denom = (cosLh * cosLh) * (alphaSq - 1.0) + 1.0;
    return alphaSq / (M_PI * denom * denom);
}

// Single term for separable Schlick-GGX below.
float gaSchlickG1(float cosTheta, float k)
{
    return cosTheta / (cosTheta * (1.0 - k) + k);
}

// Schlick-GGX approximation of geometric attenuation function using Smith's method.
float gaSchlickGGX(float cosLi, float cosLo, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // Epic suggests using this roughness remapping for analytic lights.
    return gaSchlickG1(cosLi, k) * gaSchlickG1(cosLo, k);
}

// Shlick's approximation of the Fresnel factor.
vec3 fresnelSchlick(vec3 F0, float cosTheta)
{
    return F0 + (vec3(1.0) - F0) * pow(1.0 - cosTheta, 5.0);
}

void main()
{
    vec3 albedo = texture(albedoSampler, frag_uv).rgb * material.albedoFactor.rgb;

    const float metalness = texture(MRAOSampler, frag_uv).r * material.metalnessFactor;
    const float roughness = texture(MRAOSampler, frag_uv).g * material.roughnessFactor;

    vec3 emissive = texture(emissiveSampler, frag_uv).rgb * material.emissiveFactor.rgb;

    // Remember that we are in view space, and the camera is at exactly (0,0,0).
    vec3 Lo = normalize((- camera.cameras[pc.camera_id].view * vec4(frag_position, 1.0)).xyz);
    vec3 N = normalize((camera.cameras[pc.camera_id].view * vec4(sampleWorldNormal(), 0.0)).xyz);
    float cosLo = max(0.0, dot(N, Lo));

    // Cascade selection works on the view space depth; the normal offset uses the
    // geometric normal, not the normal map's.
    const float view_depth = -(camera.cameras[pc.camera_id].view * vec4(frag_position, 1.0)).z;
    const vec3 geometric_normal = normalize(frag_normal);
    const float shadow_rotation = interleavedGradientNoise(gl_FragCoord.xy) * 2.0 * M_PI;
        
    // Specular reflection vector.
    vec3 Lr = 2.0 * cosLo * N - Lo;

    // Fresnel reflectance at normal incidence (for metals use albedo color).
    vec3 F0 = mix(vec3(0.04), albedo, metalness);

    vec3 directLighting = vec3(0);
    for(int i = 0; i < scene.noncasting_light_count; ++i)
    {
        vec3 Li = vec3(0.0f);
        if (scene.noncasting_lights.light_source[i].w == 0.0) // 0: directional light
            Li = normalize(mat3(camera.cameras[pc.camera_id].view) * -scene.noncasting_lights.light_source[i].xyz);
        else if (scene.noncasting_lights.light_source[i].w != 0.0) // 1: point light
            Li = normalize((camera.cameras[pc.camera_id].view * vec4(scene.noncasting_lights.light_source[i].xyz - frag_position, 0.0)).xyz);
        vec3 Lradiance = scene.noncasting_lights.light_color[i].rgb;
        vec3 Lh = normalize(Li + Lo);

        float cosLi = max(0.0, dot(N, Li));
        float cosLh = max(0.0, dot(N, Lh));
 
        vec3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, Lo)));
        float D = ndfGGX(cosLh, roughness);
        float G = gaSchlickGGX(cosLi, cosLo, roughness);

        // Diffuse scattering happens due to light being refracted multiple times by a dielectric medium.
        // Metals on the other hand either reflect or absorb energy, so diffuse contribution is always zero.
        // To be energy conserving we must scale diffuse BRDF contribution based on Fresnel factor & metalness.
        vec3 kd = mix(vec3(1.0) - F, vec3(0.0), metalness);

        // Lambert diffuse BRDF.
        // We don't scale by 1/PI for lighting & material units to be more convenient.
        // See: https://seblagarde.wordpress.com/2012/01/08/pi-or-not-to-pi-in-game-lighting-equation/
        vec3 diffuseBRDF = kd * albedo;

        // Cook-Torrance specular microfacet BRDF.
        vec3 specularBRDF = (F * D * G) / max(M_EPS, 4.0 * cosLi * cosLo);

        // Total contribution for this light.
        directLighting += (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
    }
    
    for(int i = 0; i < scene.casting_light_count; ++i)
    {
        // For directional lights, the light direction is fixed
        vec3 Li = normalize((mat3(camera.cameras[pc.camera_id].view) * -scene.casting_lights.light_source[i].xyz).xyz);
        vec3 Lradiance = scene.casting_lights.light_color[i].rgb;
        vec3 Lh = normalize(Li + Lo);

        float cosLi = max(0.0, dot(N, Li));
        float cosLh = max(0.0, dot(N, Lh));
 
        vec3 F  = fresnelSchlick(F0, max(0.0, dot(Lh, Lo)));
        float D = ndfGGX(cosLh, roughness);
        float G = gaSchlickGGX(cosLi, cosLo, roughness);

        // Diffuse scattering happens due to light being refracted multiple times by a dielectric medium.
        // Metals on the other hand either reflect or absorb energy, so diffuse contribution is always zero.
        // To be energy conserving we must scale diffuse BRDF contribution based on Fresnel factor & metalness.
        vec3 kd = mix(vec3(1.0) - F, vec3(0.0), metalness);

        // Lambert diffuse BRDF.
        vec3 diffuseBRDF = kd * albedo;

        // Cook-Torrance specular microfacet BRDF.
        vec3 specularBRDF = (F * D * G) / max(M_EPS, 4.0 * cosLi * cosLo);
        
        // Calculate the shadow factor with cascade selection and PCF
        float shadowCoef = calculateShadow(i, frag_position, geometric_normal, view_depth, shadow_rotation);

        // Total contribution for this light with shadow
        directLighting += shadowCoef * (diffuseBRDF + specularBRDF) * Lradiance * cosLi;
    }

    out_color = vec4(directLighting + emissive, 1.0);
}
