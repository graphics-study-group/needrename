#version 450
#extension GL_ARB_shading_language_include : require
#include "interface.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = camera.cameras[pc.camera_id].proj * camera.cameras[pc.camera_id].view * pc.model * vec4(inPosition.xyz, 1.0);
    fragColor = inColor;
}
