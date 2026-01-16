#version 450
#extension GL_ARB_shading_language_include : require
#include <engine/interface.glsl>

layout(location = 0) in vec3 vertex_position;

out gl_PerVertex 
{
    vec4 gl_Position;
};
 
void main()
{
    gl_Position = scene.casting_lights.light_vp_matrix[pc.camera_id] * pc.model * vec4(vertex_position.xyz, 1.0);
}
