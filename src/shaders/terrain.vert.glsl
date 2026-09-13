#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec3 out_normal;
layout(location = 1) out vec2 out_uv;

layout(push_constant) uniform TerrainPushConstants
{
    mat4 model_view_projection;
} terrain;

void main()
{
    gl_Position = terrain.model_view_projection * vec4(in_position, 1.0);
    out_normal = in_normal;
    out_uv = in_uv;
}
