#version 450
#extension GL_ARB_shading_language_include : require
#include <engine/interface.glsl>

layout(constant_id = 0) const int SPECULAR_SHADING_MODE = 2;

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

void main() {
    vec3 frag_position_vs = (camera.cameras[pc.camera_id].view * vec4(frag_position, 1.0)).xyz;
    // Get normalized normal vector in view space
    vec3 normal_vs = normalize(mat3(camera.cameras[pc.camera_id].view) * frag_normal);
    // Get normalized incident vector pointing from the light source
    vec3 incident_vs;
    if (scene.casting_light_count == 1) {
        incident_vs = normalize(mat3(camera.cameras[pc.camera_id].view) * scene.casting_lights.light_source[0].xyz);
    } else if (scene.noncasting_light_count == 1) {
        incident_vs = (camera.cameras[pc.camera_id].view * vec4(frag_position - scene.noncasting_lights.light_source[0].xyz, 1.0)).xyz;
    }
    
    incident_vs = normalize(incident_vs);
    // Get view position in view space
    vec3 view_position_vs = vec3(0.0, 0.0, 0.0);
    // Get view vector
    vec3 view_vs = normalize(view_position_vs - frag_position_vs);

    // Calculate diffuse coefficient
    float diffuse_coef = max(0.0, dot(-incident_vs, normal_vs));

    // Calculate specular coefficient
    float shininess = material.specular_color.w;
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

    // Sample base color
    const vec3 base_color = texture(base_tex, frag_uv).rgb * frag_color;
    const vec3 ambient = material.ambient_color.rgb * base_color;
    const vec3 light = diffuse_coef * base_color + (specular_coef * material.specular_color.rgb) * base_color;
    float shadow_coef = 1.0;
    if (scene.casting_light_count > 0) {
        // Caculate shadow
        // Determine fragment position in light space
        vec4 frag_position_ls = scene.casting_lights.light_vp_matrix[0] * vec4(frag_position, 1.0);
        frag_position_ls.xyz /= frag_position_ls.w;
        // mapping [-1, 1] to [0, 1] for sampling
        frag_position_ls.xy *= 0.5;
        frag_position_ls.xy += 0.5;
        float light_map_depth = texture(light_shadowmaps[0], frag_position_ls.xy).x;

        // Larger depth -> farther -> occluded
        shadow_coef = (light_map_depth > frag_position_ls.z) ? 1.0 : 0.0;
    }
    outColor = vec4(ambient + shadow_coef * light, 1.0);
}
