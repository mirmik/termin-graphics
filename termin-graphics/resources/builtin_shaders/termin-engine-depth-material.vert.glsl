#version 330 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

uniform mat4 u_model;
uniform mat4 u_view;
uniform mat4 u_projection;
uniform float u_near;
uniform float u_far;

out float v_linear_depth;

void main()
{
    vec4 world_pos = u_model * vec4(a_position, 1.0);
    vec4 view_pos = u_view * world_pos;

    float y = view_pos.y;
    float depth = (y - u_near) / (u_far - u_near);

    v_linear_depth = depth;
    gl_Position = u_projection * view_pos;
}
