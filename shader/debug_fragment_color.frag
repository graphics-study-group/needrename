#version 450
#extension GL_ARB_shading_language_include : require
#include "interface.glsl"

layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
