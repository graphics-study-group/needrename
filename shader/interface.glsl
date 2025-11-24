#ifndef INTERFACE_GLSL_INCLUDED
#define INTERFACE_GLSL_INCLUDED

#define MAX_LIGHTS 8
#define MAX_CAMERAS 16

struct LightAttributeStruct {
    uint light_count;
    vec4 light_source[MAX_LIGHTS];
    vec4 light_color[MAX_LIGHTS];
    mat4 light_vp_matrix[MAX_LIGHTS];
};

layout(set = 0, binding = 0) uniform PerSceneUniform {
    LightAttributeStruct lights;
} scene;

struct CameraBufferStruct {
    mat4 view;
    mat4 proj;
};

layout(set = 1, binding = 0) uniform CameraBuffer{
    CameraBufferStruct cameras[MAX_CAMERAS];
} camera;

layout(push_constant) uniform ModelTransform {
    mat4 model;
    int camera_id;
} pc;

#endif // INTERFACE_GLSL_INCLUDED
