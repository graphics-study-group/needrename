#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexcoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 outTexcoord;

void main() {
    gl_Position = vec4(inPosition.xy, 0.0, 1.0);
    fragColor = inColor;
    outTexcoord = inTexcoord;
}
