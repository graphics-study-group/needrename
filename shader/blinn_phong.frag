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

layout(set = 1, binding = 0) uniform CameraBuffer{
    mat4 view;
    mat4 proj;
} camera;

layout(set = 2, binding = 0) uniform sampler2D base_tex;
layout(set = 2, binding = 1) uniform Material {
    vec4 specular_color;
    vec4 ambient_color;
} material;

void main() {
    // Get normalized normal vector in world space
    vec3 normal = normalize(frag_normal);
    // Get normalized incident vector pointing from the light source
    vec3 incident = normalize(frag_position - scene.light_source);
    // Get view position in world space
    // TODO: Inversing a matrix is inefficient, consider passing it as uniform
    vec3 view_position = (inverse(camera.view) * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
    // Get view vector
    vec3 view = normalize(view_position - frag_position);

    // Calculate diffuse coefficient
    float diffuse_coef = max(0.0, dot(-incident, normal));

    // Calculate specular coefficient
    float shininess = material.specular_color.w;
    vec3 reflected = reflect(incident, normal);
    float specular_coef = pow(max(0.0, dot(view, reflected)), shininess);

    // Sample base color
    vec3 base_color = texture(base_tex, frag_uv).rgb * frag_color;

    outColor = vec4(
        diffuse_coef * base_color + 
        material.ambient_color.rgb * base_color +
        (specular_coef * material.specular_color.rgb) * base_color
        ,1.0);
}
