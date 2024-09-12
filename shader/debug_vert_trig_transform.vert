#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

layout(set = 0, binding = 0) uniform CameraBuffer{
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform ModelTransform {
    mat4 model;
} modelTransform;

void main() {
    gl_Position = camera.proj * camera.view * modelTransform.model * vec4(inPosition.xy, 0.0, 1.0);
    fragColor = inColor;
}
