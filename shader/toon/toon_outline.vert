#version 450
#extension GL_ARB_shading_language_include : require
#include "../interface.glsl"

layout(location = 0) in vec3 position;
layout(location = 2) in vec3 normal;

void main() {
    vec3 new_position = position + normal * 0.01;
    gl_Position = camera.cameras[pc.camera_id].proj * camera.cameras[pc.camera_id].view * pc.model * vec4(new_position, 1.0);
}
