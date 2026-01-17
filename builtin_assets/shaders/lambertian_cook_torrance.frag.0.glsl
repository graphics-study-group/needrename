/*
 * Lambertian diffuse + Cook-Torrance microfacet specular BSDF.
 * Modified from https://github.com/Nadrin/PBR/blob/master/data/shaders/glsl/pbr_fs.glsl.
 */
#version 450 core
#extension GL_ARB_shading_language_include : require
#include <engine/interface.glsl>

#define M_PI 3.1415926538
#define M_EPS (1e-5)

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in vec3 frag_position;

layout(location = 0) out vec4 out_color;

layout(std140, set = 2, binding = 0) uniform Material {
    float metalness;
    float roughness;
} material;
layout (set=2, binding=1) uniform sampler2D albedoSampler;

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
    vec3 albedo = texture(albedoSampler, frag_uv).rgb;

    const float metalness = material.metalness;
    const float roughness = material.roughness;

    // Remember that we are in view space, and the camera is at exactly (0,0,0).
    vec3 Lo = normalize((- camera.cameras[pc.camera_id].view * vec4(frag_position, 1.0)).xyz);
    vec3 N = (camera.cameras[pc.camera_id].view * vec4(frag_normal, 0.0)).xyz;
    float cosLo = max(0.0, dot(N, Lo));
        
    // Specular reflection vector.
    vec3 Lr = 2.0 * cosLo * N - Lo;

    // Fresnel reflectance at normal incidence (for metals use albedo color).
    vec3 F0 = mix(vec3(0.04), albedo, metalness);

    vec3 directLighting = vec3(0);
    for(int i = 0; i < scene.noncasting_light_count; ++i)
    {
        vec3 Li = -(camera.cameras[pc.camera_id].view * vec4(frag_position - scene.noncasting_lights.light_source[i].xyz, 0.0)).xyz;
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

    out_color = vec4(directLighting, 1.0);
}
