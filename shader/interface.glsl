#ifndef INTERFACE_GLSL_INCLUDED
#define INTERFACE_GLSL_INCLUDED

#define MAX_SHADOW_CASTING_LIGHTS 8
#define MAX_NON_CASTING_LIGHTS 16
#define MAX_CAMERAS 16

struct LightAttributeStruct {
    vec4 light_source[MAX_SHADOW_CASTING_LIGHTS];
    vec4 light_color[MAX_SHADOW_CASTING_LIGHTS];
    mat4 light_vp_matrix[MAX_SHADOW_CASTING_LIGHTS];
};

struct NonCastingLightAttributeStruct {
    vec4 light_source[MAX_NON_CASTING_LIGHTS];
    vec4 light_color[MAX_NON_CASTING_LIGHTS];
};

layout(set = 0, binding = 0) uniform PerSceneUniform {
    uint casting_light_count;
    uint noncasting_light_count;
    LightAttributeStruct casting_lights;
    NonCastingLightAttributeStruct noncasting_lights;
} scene;
layout(set = 0, binding = 1) uniform sampler2D light_shadowmaps[MAX_SHADOW_CASTING_LIGHTS];

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
