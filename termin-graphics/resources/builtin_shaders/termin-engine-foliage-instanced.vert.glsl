#version 330 core

layout(std140, binding = 2) uniform PerFrame {
    mat4 u_view;
    mat4 u_projection;
    mat4 u_view_projection;
    mat4 u_inv_view;
    mat4 u_inv_proj;
    vec4 u_camera_position;
    vec2 u_resolution;
    float u_near;
    float u_far;
};

struct FoliagePushData {
    mat4 u_position_model;
    mat4 u_vector_model;
};
#ifdef VULKAN
layout(push_constant) uniform FoliagePushBlock { FoliagePushData pc; };
#else
layout(std140, binding = 14) uniform FoliagePushBlock { FoliagePushData pc; };
#endif

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(location = 8) in vec3 i_position;
layout(location = 9) in float i_yaw;
layout(location = 10) in vec3 i_normal;
layout(location = 11) in float i_seed;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;
out mat3 v_TBN;
out vec3 v_tangent;
out float v_foliage_seed;

void main() {
    vec3 up = normalize(i_normal);
    // Keep yaw=0 aligned with ordinary mesh rendering on flat Z-up surfaces:
    // local X -> world X, local Y -> world Y, local Z -> world Z.
    vec3 helper = abs(up.z) > 0.85 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 right = normalize(cross(helper, up));
    vec3 forward = cross(up, right);

    float c = cos(i_yaw);
    float s = sin(i_yaw);
    vec3 yaw_right = right * c + forward * s;
    vec3 yaw_forward = -right * s + forward * c;
    vec3 local_offset =
        yaw_right * a_position.x +
        yaw_forward * a_position.y +
        up * a_position.z;

    vec3 source_normal = length(a_normal) > 0.0001 ? normalize(a_normal) : vec3(0.0, 0.0, 1.0);
    vec3 local_normal = normalize(
        yaw_right * source_normal.x +
        yaw_forward * source_normal.y +
        up * source_normal.z
    );
    vec3 world_pos =
        (pc.u_position_model * vec4(i_position, 1.0)).xyz +
        (pc.u_vector_model * vec4(local_offset, 0.0)).xyz;
    vec3 world_normal = normalize((pc.u_vector_model * vec4(local_normal, 0.0)).xyz);

    v_world_pos = world_pos;
    v_normal = world_normal;
    v_uv = a_uv;
    v_TBN = mat3(0.0);
    v_tangent = vec3(0.0);
    v_foliage_seed = fract(i_seed * 0.61803398875);
    gl_Position = u_view_projection * vec4(world_pos, 1.0);
}
