#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal;
layout(location = 3) in vec3 frag_position;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform Scene {
    vec3 light_source;
    vec3 light_color;
} scene;

layout(set = 2, binding = 0) uniform sampler2D diffuse;
layout(set = 2, binding = 1) uniform Material {
    vec3 specular;
    vec3 ambient;
} material;

void main() {
    vec3 normal = normalize(frag_normal);
    vec3 light_dir = normalize(frag_position - scene.light_source);
    float diffuse_strength = max(0.0, dot(light_dir, normal));
    vec3 sampled_color = texture(diffuse, frag_uv).rgb;
    outColor = vec4((diffuse_strength * sampled_color + material.ambient * sampled_color) * frag_color, 1.0) ;
}
