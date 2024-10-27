#version 450

layout(location = 0) in vec3 vertex_position;
layout(location = 1) in vec3 vertex_color;
layout(location = 2) in vec3 vertex_normal;
layout(location = 3) in vec2 vertex_uv_0;

layout(location = 0) out vec3 to_frag_color;
layout(location = 1) out vec2 to_frag_uv_0;
layout(location = 2) out vec3 to_frag_normal;
layout(location = 3) out vec3 to_frag_position;

layout(set = 1, binding = 0) uniform CameraBuffer{
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform ModelTransform {
    mat4 model;
} modelTransform;

void main() {
    gl_Position = camera.proj * camera.view * modelTransform.model * vec4(vertex_position.xyz, 1.0);

    to_frag_color = vertex_color;
    to_frag_uv_0 = vertex_uv_0;

    // Transform normal to world space
    // TODO: Consider non-uniform scaling
    to_frag_normal = mat3(modelTransform.model) * vertex_normal;

    // Get world space fragment position
    to_frag_position = vec3(modelTransform.model * vec4(vertex_position.xyz, 1.0));
}
