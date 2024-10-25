#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexcoord;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D Sampler;

void main() {
    outColor = vec4(texture(Sampler, fragTexcoord).rgb * fragColor, 1.0) ;
}
