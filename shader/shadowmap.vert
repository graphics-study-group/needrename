#version 450

layout(location = 0) in vec3 vertex_position;

layout(set = 1, binding = 0) uniform CameraBuffer{
    mat4 view;
    mat4 proj;
} camera;

layout(push_constant) uniform ModelTransform {
    mat4 model;
} modelTransform;

out gl_PerVertex 
{
    vec4 gl_Position;
};

 
void main()
{
    gl_Position = camera.proj * camera.view * modelTransform.model * vec4(vertex_position.xyz, 1.0);
}
