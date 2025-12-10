#version 450
#extension GL_ARB_shading_language_include : require
#include "../interface.glsl"

layout(constant_id = 0) const float FRESNEL_POWER = 5;

layout(set = 2, binding = 0) uniform Material {
    vec3 rim_light_color;
} mat;

layout(set = 2, binding = 1) uniform sampler2D base_texture;
layout(set = 2, binding = 2) uniform sampler1D ramp_texture;

// Vertex color is ignored for now
layout(location = 0) in vec3 vert_color;
layout(location = 1) in vec2 uv_0;
layout(location = 2) in vec3 normal_ws;
layout(location = 3) in vec3 position_ws;

layout(location = 0) out vec3 frag_color;

void main() {
    // Caculate basic info
    vec3 position_vs = (camera.cameras[pc.camera_id].view * vec4(position_ws, 1.0)).xyz;
    vec3 normal_vs = normalize(mat3(camera.cameras[pc.camera_id].view) * normal_ws);
    vec3 incident_vs = (camera.cameras[pc.camera_id].view * vec4(position_ws - scene.noncasting_lights.light_source[0].xyz, 1.0)).xyz;
    incident_vs = normalize(incident_vs);

    // Determine base color
    vec3 base_color = texture(base_texture, uv_0).rgb;
    float ramp_coef = dot(normal_vs, incident_vs);
    vec3 ramp_color = texture(ramp_texture, ramp_coef).rgb;

    frag_color = base_color * ramp_color * scene.noncasting_lights.light_color[0].rgb;

    // View vector in view space
    vec3 view_vs = normalize(vec3(0.0, 0.0, 0.0) - position_vs);

    // Determine Fresnel Rim light
    float NdotV = clamp(dot(view_vs, normal_vs), 0.0, 1.0);
    float fresnel = pow((1 - NdotV), FRESNEL_POWER);
    fresnel = smoothstep(0.0, 1.0, fresnel);
    frag_color += fresnel * mat.rim_light_color;
}

