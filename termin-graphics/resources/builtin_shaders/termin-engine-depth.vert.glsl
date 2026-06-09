#version 450 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_texcoord;

layout(std140, binding = 0) uniform PerFrame {
    mat4  u_view;
    mat4  u_projection;
    float u_near;
    float u_far;
    float u_depth_encoding;
};

struct DepthPushData {
    mat4 u_model;
};
#ifdef VULKAN
layout(push_constant) uniform DepthPushBlock { DepthPushData pc; };
#else
layout(std140, binding = 14) uniform DepthPushBlock { DepthPushData pc; };
#endif

layout(location = 0) out float v_linear_depth;
layout(location = 1) out float v_perspective_depth;
layout(location = 2) out float v_log_depth;

void main() {
    vec4 world_pos = pc.u_model * vec4(a_position, 1.0);
    vec4 view_pos  = u_view * world_pos;

    float view_depth = view_pos.y;
    float depth = (view_depth - u_near) / (u_far - u_near);

    vec4 clip_pos = u_projection * view_pos;
    float ndc_depth = clip_pos.z / max(abs(clip_pos.w), 1e-6);
    float perspective_depth = ndc_depth;

    v_linear_depth = depth;
    v_perspective_depth = perspective_depth;
    v_log_depth = log2(max(view_depth, 0.0) + 1.0) / log2(max(u_far, 1e-6) + 1.0);
    gl_Position = clip_pos;
}
