#version 450
#extension GL_ARB_shading_language_include : require
#include "../interface.glsl"

layout(location = 0) out vec3 color;

void main() {
    color = vec3(0.0f, 0.0f, 0.0f);
}
