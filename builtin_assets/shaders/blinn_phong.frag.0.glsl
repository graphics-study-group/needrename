#version 450
#extension GL_ARB_shading_language_include : require
#include <engine/interface.glsl>
#include <engine/shadow.glsl>

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
