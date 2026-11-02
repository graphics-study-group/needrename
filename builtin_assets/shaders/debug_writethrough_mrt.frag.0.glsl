// Write through fragment data to seperate render targets
#version 450 core 

layout(location = 0) in vec3 to_frag_position;
layout(location = 1) in vec3 to_frag_color;
layout(location = 2) in vec3 to_frag_normal;
layout(location = 3) in vec2 to_frag_uv_0;

// Four-component outputs: the colour attachments are four-component formats, and a backend
// whose shader language checks the interface rejects a vec3 output where the target has four
// channels instead of widening it the way Vulkan does.
layout(location = 0) out vec4 position_rt;
layout(location = 1) out vec4 color_rt;
layout(location = 2) out vec4 normal_rt;
layout(location = 3) out vec4 uv_rt;

void main()  {
    position_rt = vec4(to_frag_position, 1.0);
    color_rt = vec4(to_frag_color, 1.0);
    normal_rt = vec4(to_frag_normal, 1.0);
    uv_rt = vec4(to_frag_uv_0, 0.0, 1.0);
}
