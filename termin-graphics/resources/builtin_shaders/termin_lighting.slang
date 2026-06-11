import termin_prelude;

public static const int LIGHT_TYPE_DIRECTIONAL = 0;
public static const int LIGHT_TYPE_POINT = 1;
public static const int LIGHT_TYPE_SPOT = 2;
public static const int MAX_LIGHTS = 8;

public struct LightData {
    float4 color_intensity;
    float4 direction_range;
    float4 position_type;
    float4 attenuation_inner;
    float4 outer_cascade;
};

public struct LightingBlock {
    LightData u_lights[MAX_LIGHTS];
    float4 u_ambient_data;
    float4 u_camera_light_count;
    float4 u_shadow_settings;
};

[[TerminScope("pass")]]
public ConstantBuffer<LightingBlock> lighting;

public int get_light_count() { return int(lighting.u_camera_light_count.w); }
public int get_light_type(int i) { return int(lighting.u_lights[i].position_type.w); }
public float3 get_light_color(int i) { return lighting.u_lights[i].color_intensity.rgb; }
public float get_light_intensity(int i) { return lighting.u_lights[i].color_intensity.w; }
public float3 get_light_direction(int i) { return lighting.u_lights[i].direction_range.xyz; }
public float3 get_light_position(int i) { return lighting.u_lights[i].position_type.xyz; }
public float get_light_range(int i) { return lighting.u_lights[i].direction_range.w; }
public float3 get_light_attenuation(int i) { return lighting.u_lights[i].attenuation_inner.xyz; }
public float get_light_inner_angle(int i) { return lighting.u_lights[i].attenuation_inner.w; }
public float get_light_outer_angle(int i) { return lighting.u_lights[i].outer_cascade.x; }
public float3 get_ambient_color() { return lighting.u_ambient_data.rgb; }
public float get_ambient_intensity() { return lighting.u_ambient_data.w; }
public float3 get_camera_position() { return lighting.u_camera_light_count.xyz; }
public float get_shadow_bias() { return lighting.u_shadow_settings.z; }

public float compute_distance_attenuation(float3 attenuation, float range, float dist) {
    float denom = attenuation.x + attenuation.y * dist + attenuation.z * dist * dist;
    if (denom <= 0.0) {
        return 1.0;
    }
    float weight = 1.0 / denom;
    if (range > 0.0 && dist > range) {
        weight = 0.0;
    }
    return weight;
}

public float compute_spot_weight(float3 light_dir, float3 L, float inner_angle, float outer_angle) {
    float cos_theta = dot(light_dir, -L);
    float cos_outer = cos(outer_angle);
    float cos_inner = cos(inner_angle);

    if (cos_theta <= cos_outer) return 0.0;
    if (cos_theta >= cos_inner) return 1.0;

    float t = (cos_theta - cos_outer) / (cos_inner - cos_outer);
    return t * t * (3.0 - 2.0 * t);
}
