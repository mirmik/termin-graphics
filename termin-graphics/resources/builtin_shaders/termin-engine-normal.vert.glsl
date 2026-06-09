#version 450 core
#extension GL_ARB_shading_language_420pack : require

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;

layout(std140, binding = 0) uniform PerFrame {
    mat4 u_view;
    mat4 u_projection;
};

struct NormalPushData {
    mat4 u_model;
};
#ifdef VULKAN
layout(push_constant) uniform NormalPushBlock { NormalPushData pc; };
#else
layout(std140, binding = 14) uniform NormalPushBlock { NormalPushData pc; };
#endif

layout(location = 0) out vec3 v_world_normal;

void main() {
    mat3 normal_matrix = transpose(inverse(mat3(pc.u_model)));
    v_world_normal = normalize(normal_matrix * a_normal);

    vec4 world_pos = pc.u_model * vec4(a_position, 1.0);
    gl_Position = u_projection * u_view * world_pos;
}
