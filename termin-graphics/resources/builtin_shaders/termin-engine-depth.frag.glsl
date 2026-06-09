#version 450 core

layout(location = 0) in float v_linear_depth;
layout(location = 1) in float v_perspective_depth;
layout(location = 2) in float v_log_depth;
layout(location = 0) out vec4 FragColor;

layout(std140, binding = 0) uniform PerFrame {
    mat4  u_view;
    mat4  u_projection;
    float u_near;
    float u_far;
    float u_depth_encoding;
};

void main() {
    int mode = int(u_depth_encoding + 0.5);
    float d = v_linear_depth;
    if (mode == 2 || mode == 3) {
        d = v_perspective_depth;
    } else if (mode == 4 || mode == 5) {
        d = v_log_depth;
    }
    d = clamp(d, 0.0, 1.0);
    if (mode == 1 || mode == 3 || mode == 5) {
        d = 1.0 - d;
    }
    FragColor = vec4(d, d, d, 1.0);
}
