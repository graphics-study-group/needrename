#version 450
#extension GL_ARB_shading_language_include : require
#include "interface.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 4) in vec2 inTexcoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 outTexcoord;

void main() {
    gl_Position = camera.cameras[pc.camera_id].proj * camera.cameras[pc.camera_id].view * pc.model * vec4(inPosition.xyz, 1.0);
    fragColor = inColor;
    outTexcoord = inTexcoord;
}
