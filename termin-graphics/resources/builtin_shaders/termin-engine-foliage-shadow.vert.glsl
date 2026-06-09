#version 450 core

layout(location = 0) in vec3 a_position;

layout(location = 8) in vec3 i_position;
layout(location = 9) in float i_yaw;
layout(location = 10) in vec3 i_normal;

layout(std140, binding = 0) uniform PerFrame {
    mat4 u_view;
    mat4 u_projection;
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

    vec3 world_pos =
        (pc.u_position_model * vec4(i_position, 1.0)).xyz +
        (pc.u_vector_model * vec4(local_offset, 0.0)).xyz;
    gl_Position = u_projection * u_view * vec4(world_pos, 1.0);
}
