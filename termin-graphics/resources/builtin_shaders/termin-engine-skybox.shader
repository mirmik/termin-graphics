@program Skybox

@phase opaque
@priority 0
@glDepthTest true
@glDepthMask false
@glCull false

@property Mat4  u_view
@property Mat4  u_projection
@property Int   u_skybox_type
@property Color u_skybox_color        = Color(0.5, 0.5, 0.5, 1.0)
@property Color u_skybox_top_color    = Color(0.3, 0.5, 1.0, 1.0)
@property Color u_skybox_bottom_color = Color(0.1, 0.1, 0.3, 1.0)

@stage vertex
#version 450 core
layout(location = 0) in vec3 a_position;

uniform mat4 u_view;
uniform mat4 u_projection;

layout(location = 0) out vec3 v_dir;

void main() {
    mat4 view_no_translation = mat4(mat3(u_view));
    v_dir = a_position;
    gl_Position = u_projection * view_no_translation * vec4(a_position, 1.0);
}
@endstage

@stage fragment
#version 450 core

layout(location = 0) in vec3 v_dir;
layout(location = 0) out vec4 FragColor;

uniform int  u_skybox_type;
uniform vec4 u_skybox_color;
uniform vec4 u_skybox_top_color;
uniform vec4 u_skybox_bottom_color;

void main() {
    // 0 = gradient, 1 = solid - matches the TC_SKYBOX_* enum values in
    // core/tc_scene_skybox.h (TC_SKYBOX_GRADIENT=0, TC_SKYBOX_SOLID=1;
    // TC_SKYBOX_NONE is filtered out by the C++ caller before dispatch).
    if (u_skybox_type == 1) {
        FragColor = vec4(u_skybox_color.rgb, 1.0);
    } else {
        float t = normalize(v_dir).z * 0.5 + 0.5;
        vec3 c = mix(u_skybox_bottom_color.rgb, u_skybox_top_color.rgb, t);
        FragColor = vec4(c, 1.0);
    }
}
@endstage

@endphase
