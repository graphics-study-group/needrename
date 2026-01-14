// Write through fragment data to seperate render targets
#version 450 core 

layout(location = 0) in vec3 to_frag_position;
layout(location = 1) in vec3 to_frag_color;
layout(location = 2) in vec3 to_frag_normal;
layout(location = 3) in vec2 to_frag_uv_0;

layout(location = 0) out vec3 position_rt;
layout(location = 1) out vec3 color_rt;
layout(location = 2) out vec3 normal_rt;
layout(location = 3) out vec2 uv_rt;

void main()  {
    position_rt = to_frag_position;
    color_rt = to_frag_color;
    normal_rt = to_frag_normal;
    uv_rt = to_frag_uv_0;
}
