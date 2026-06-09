#version 450
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 v_color;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    vec4 camera_pos;
} u_camera;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

void main() {
    v_color = in_color;
    gl_Position = u_camera.view_proj * pc.model * vec4(in_position, 1.0);
}
