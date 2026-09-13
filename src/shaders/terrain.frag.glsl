#version 450

layout(location = 0) in vec3 in_normal;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D diffuse_texture;
layout(set = 0, binding = 1) uniform sampler2D normal_texture;

void main()
{
    vec3 light_direction = normalize(vec3(0.35, 0.45, 0.82));
    float diffuse = max(dot(normalize(in_normal), light_direction), 0.0);
    vec3 terrain_color = texture(diffuse_texture, in_uv).rgb;
    vec3 normal_map = texture(normal_texture, in_uv).rgb * 2.0 - 1.0;
    normal_map = normalize(normal_map);
    diffuse = max(dot(normalize(mix(normalize(in_normal), normal_map, 0.25)), light_direction), 0.0);
    out_color = vec4(terrain_color * (0.25 + 0.75 * diffuse), 1.0);
}
