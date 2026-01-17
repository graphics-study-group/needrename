#version 450

layout(set = 0, binding = 0) uniform samplerCube skybox;

layout(location = 0) in vec3 texture_coordinate;
layout(location = 0) out vec4 color;

void main () {
    color = texture(skybox, texture_coordinate);
}
