#ifndef INTERFACE_GLSL_INCLUDED
#define INTERFACE_GLSL_INCLUDED

#define MAX_SHADOW_CASTING_LIGHTS 1
#define MAX_SHADOW_CASCADES 4
#define MAX_NON_SHADOW_CASTING_LIGHTS 16
#define MAX_CAMERAS 16

struct LightAttributeStruct {
    vec4 light_source[MAX_SHADOW_CASTING_LIGHTS];
    vec4 light_color[MAX_SHADOW_CASTING_LIGHTS];
    /**
     * Projection * view matrix of every cascade of every light, indexed
     * `light * MAX_SHADOW_CASCADES + cascade`.
     */
    mat4 light_vp_matrix[MAX_SHADOW_CASTING_LIGHTS * MAX_SHADOW_CASCADES];
    /**
     * Cascade end distances measured from the camera along its view axis. The
     * component at `c + 1` ends cascade `c`, so the light's effective shadow
     * distance is the last component the light's cascade count covers.
     */
    vec4 cascade_splits[MAX_SHADOW_CASTING_LIGHTS];
    /// Number of fitted cascades per light; 0 means the light casts no shadow.
    uint light_cascade_count[MAX_SHADOW_CASTING_LIGHTS];
    /**
     * Scale and bias from a light space clip position to shadow map coordinates:
     * `uv = clip.xy * shadow_uv_scale_bias.xy + shadow_uv_scale_bias.zw`.
     *
     * The x scale and both biases are 0.5. The y scale carries the backend's
     * framebuffer convention instead of a constant here: a framebuffer row runs
     * the same way as a texture's v axis, but Vulkan's clip space points y down
     * while the standard WebGPU clip space points y up. Sampling with the wrong
     * sign mirrors the map about light space y, so an object that moves one way
     * casts a shadow that moves the other way.
     */
    vec4 shadow_uv_scale_bias[MAX_SHADOW_CASTING_LIGHTS];
};

struct NonCastingLightAttributeStruct {
    vec4 light_source[MAX_NON_SHADOW_CASTING_LIGHTS];
    vec4 light_color[MAX_NON_SHADOW_CASTING_LIGHTS];
};

layout(std140, set = 0, binding = 0) uniform PerSceneUniform {
    uint casting_light_count;
    uint noncasting_light_count;
    LightAttributeStruct casting_lights;
    NonCastingLightAttributeStruct noncasting_lights;
} scene;
// One shadow map per casting light, each an array of cascades. A single binding rather than
// an array of them: a backend whose shader language has no combined image sampler cannot
// express an array of handles, and MAX_SHADOW_CASTING_LIGHTS is 1.
//
// Binding 1 is the texture and binding 2 is its sampler: a runtime that splits the pair
// declares them as two bindings, and the split slot is the one after the texture.
layout(set = 0, binding = 1) uniform sampler2DArrayShadow light_shadowmaps;

layout(set = 0, binding = 3) readonly buffer ModelMatrices {
    mat4 m[];
} model_matrices;

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
    int model_mat_index;
} pc;

/**
 * @brief Resolve the model matrix to use for the current draw.
 *
 * If model_mat_index is non-negative, the physics-driven rigid body center
 * matrix is composed with the per-draw local transform (push-constant model,
 * which carries the renderer's local offset relative to the rigid body's GO).
 * Otherwise the per-draw push-constant model matrix is used as the full world
 * transform.
 */
mat4 get_model_matrix() {
    if (pc.model_mat_index >= 0) {
        return model_matrices.m[pc.model_mat_index] * pc.model;
    }
    return pc.model;
}

#endif // INTERFACE_GLSL_INCLUDED
