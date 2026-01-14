layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 1) uniform sampler2D base_tex;
layout(set = 2, binding = 0) uniform Material {
    vec4 specular_color;
    vec4 ambient_color;
} material;
