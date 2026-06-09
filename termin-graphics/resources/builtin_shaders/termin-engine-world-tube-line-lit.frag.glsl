#version 450 core
const int LIGHT_TYPE_DIRECTIONAL = 0;
const int LIGHT_TYPE_POINT = 1;
const int LIGHT_TYPE_SPOT = 2;
const int MAX_LIGHTS = 8;

struct LightData {
    vec4 color_intensity;
    vec4 direction_range;
    vec4 position_type;
    vec4 attenuation_inner;
    vec4 outer_cascade;
};

layout(std140, binding = 0) uniform LightingBlock {
    LightData u_lights[MAX_LIGHTS];
    vec4 u_ambient_data;
    vec4 u_camera_light_count;
    vec4 u_shadow_settings;
};

float distance_attenuation(vec3 attenuation, float range, float dist) {
    float denom = attenuation.x + attenuation.y * dist + attenuation.z * dist * dist;
    float value = denom > 0.0 ? 1.0 / denom : 1.0;
    if (range > 0.0 && dist > range) {
        value = 0.0;
    }
    return value;
}

float spot_weight(vec3 light_dir, vec3 L, float inner_angle, float outer_angle) {
    float cos_theta = dot(light_dir, -L);
    float cos_outer = cos(outer_angle);
    float cos_inner = cos(inner_angle);
    if (cos_theta <= cos_outer) {
        return 0.0;
    }
    if (cos_theta >= cos_inner) {
        return 1.0;
    }
    float t = (cos_theta - cos_outer) / max(cos_inner - cos_outer, 1.0e-4);
    return t * t * (3.0 - 2.0 * t);
}

vec3 apply_tube_lighting(vec3 base_color, vec3 normal, vec3 world_pos) {
    vec3 N = normalize(normal);
    vec3 result = base_color * u_ambient_data.rgb * u_ambient_data.w;
    int count = min(int(u_camera_light_count.w), MAX_LIGHTS);
    for (int i = 0; i < count; ++i) {
        int type = int(u_lights[i].position_type.w);
        vec3 L;
        float weight = 1.0;
        if (type == LIGHT_TYPE_DIRECTIONAL) {
            L = normalize(-u_lights[i].direction_range.xyz);
        } else {
            vec3 to_light = u_lights[i].position_type.xyz - world_pos;
            float dist = length(to_light);
            L = dist > 1.0e-4 ? to_light / dist : vec3(0.0, 1.0, 0.0);
            weight *= distance_attenuation(
                u_lights[i].attenuation_inner.xyz,
                u_lights[i].direction_range.w,
                dist);
            if (type == LIGHT_TYPE_SPOT) {
                weight *= spot_weight(
                    u_lights[i].direction_range.xyz,
                    L,
                    u_lights[i].attenuation_inner.w,
                    u_lights[i].outer_cascade.x);
            }
        }
        float ndotl = abs(dot(N, L));
        result += base_color * u_lights[i].color_intensity.rgb * u_lights[i].color_intensity.w * ndotl * weight;
    }
    return result;
}

layout(location = 0) in vec3 v_world_pos;
layout(location = 1) in vec3 v_normal;
layout(location = 3) in vec4 v_color;
layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = vec4(apply_tube_lighting(v_color.rgb, v_normal, v_world_pos), v_color.a);
}
