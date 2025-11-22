layout(set = 0, binding = 0) uniform PerSceneUniform {
    uint light_count;
    vec4 light_source[8];
    vec4 light_color[8];
} scene;


struct CameraBufferStruct {
    mat4 view;
    mat4 proj;
};

layout(set = 1, binding = 0) uniform CameraBuffer{
    CameraBufferStruct cameras[8];
} camera;

layout(push_constant) uniform ModelTransform {
    mat4 model;
    int camera_id;
} pc;
