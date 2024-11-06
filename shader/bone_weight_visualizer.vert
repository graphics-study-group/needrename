#version 450

layout(location = 0) in vec3 inPosition;

layout(location = 4) in ivec4 in_bone_id;
layout(location = 5) in vec4 in_bone_weight;

layout(location = 0) out vec3 fragColor;

layout(set = 1, binding = 0) uniform CameraBuffer{
    mat4 view;
    mat4 proj;
} camera;

layout(set = 2, binding = 0) uniform BoneIdBuffer {
    int bone_id;
} boneId;

layout(push_constant) uniform ModelTransform {
    mat4 model;
} modelTransform;


void main() {
    gl_Position = camera.proj * camera.view * modelTransform.model * vec4(inPosition.xy, 0.0, 1.0);
    
    float selected_weight = 0.0;
    if (in_bone_id.x == boneId.bone_id) selected_weight = in_bone_weight.x;
    else if (in_bone_id.y == boneId.bone_id) selected_weight = in_bone_weight.y;
    else if (in_bone_id.z == boneId.bone_id) selected_weight = in_bone_weight.z;
    else if (in_bone_id.w == boneId.bone_id) selected_weight = in_bone_weight.w;

    fragColor = vec3(selected_weight, 0.5, 0.5);
}
