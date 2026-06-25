import termin_prelude;
import termin_lighting;

struct TubeLineDraw {
    float4x4 u_view_projection;
    float4 u_up_hint;
};

[[TerminScope("draw")]]
ConstantBuffer<TubeLineDraw> tube_line_draw;

struct TubeSegmentInput {
    float3 corner : TEXCOORD0;
    float3 p0 : POSITION0;
    float width : TEXCOORD1;
    float3 p1 : POSITION1;
    float4 color : COLOR0;
};

struct TubeCapInput {
    float3 corner : TEXCOORD0;
    float3 center : POSITION0;
    float width : TEXCOORD1;
    float3 neighbor : POSITION1;
    float4 color : COLOR0;
};

struct TubeOutput {
    float4 position : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal_world : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 tangent_world : TEXCOORD3;
    float3 bitangent_world : TEXCOORD4;
    float tbn_valid : TEXCOORD5;
    float4 color : COLOR0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

float3 safe_normalize(float3 v, float3 fallback) {
    float len = length(v);
    if (len < 1.0e-6) {
        return fallback;
    }
    return v / len;
}

float3 basis_axis0(float3 dir) {
    float3 up = safe_normalize(tube_line_draw.u_up_hint.xyz, float3(0.0, 1.0, 0.0));
    float3 axis0 = cross(up, dir);
    float len = length(axis0);
    if (len >= 1.0e-6) {
        return axis0 / len;
    }

    axis0 = cross(float3(1.0, 0.0, 0.0), dir);
    len = length(axis0);
    if (len >= 1.0e-6) {
        return axis0 / len;
    }

    return safe_normalize(cross(float3(0.0, 0.0, 1.0), dir), float3(1.0, 0.0, 0.0));
}

float3 tube_offset(float3 dir, float axis0_factor, float axis1_factor, float radius) {
    float3 axis0 = basis_axis0(dir);
    float3 axis1 = safe_normalize(cross(dir, axis0), float3(0.0, 1.0, 0.0));
    return (axis0 * axis0_factor + axis1 * axis1_factor) * radius;
}

float3 tube_normal(float3 dir, float axis0_factor, float axis1_factor) {
    return safe_normalize(
        tube_offset(dir, axis0_factor, axis1_factor, 1.0),
        float3(0.0, 1.0, 0.0));
}

[shader("vertex")]
TubeOutput vs_main(TubeSegmentInput input) {
    float3 dir = safe_normalize(input.p1 - input.p0, float3(1.0, 0.0, 0.0));
    float3 base = lerp(input.p0, input.p1, input.corner.x);
    float3 expanded = base + tube_offset(dir, input.corner.y, input.corner.z, input.width * 0.5);

    TubeOutput output;
    output.position = termin_to_native_clip(mul(tube_line_draw.u_view_projection, float4(expanded, 1.0)));
    output.world_pos = expanded;
    output.normal_world = tube_normal(dir, input.corner.y, input.corner.z);
    output.uv = float2(input.corner.x, atan2(input.corner.z, input.corner.y) / 6.28318530718 + 0.5);
    output.tangent_world = float3(0.0, 0.0, 0.0);
    output.bitangent_world = float3(0.0, 0.0, 0.0);
    output.tbn_valid = 0.0;
    output.color = input.color;
    return output;
}

[shader("vertex")]
TubeOutput vs_cap_main(TubeCapInput input) {
    float3 dir = safe_normalize(input.neighbor - input.center, float3(1.0, 0.0, 0.0));
    float3 expanded = input.center;
    if (input.corner.x < 0.5) {
        expanded += tube_offset(dir, input.corner.y, input.corner.z, input.width * 0.5);
    }

    TubeOutput output;
    output.position = termin_to_native_clip(mul(tube_line_draw.u_view_projection, float4(expanded, 1.0)));
    output.world_pos = expanded;
    output.normal_world = input.corner.x < 0.5
        ? tube_normal(dir, input.corner.y, input.corner.z)
        : -dir;
    output.uv = input.corner.x < 0.5
        ? float2(input.corner.y * 0.5 + 0.5, input.corner.z * 0.5 + 0.5)
        : float2(0.5, 0.5);
    output.tangent_world = float3(0.0, 0.0, 0.0);
    output.bitangent_world = float3(0.0, 0.0, 0.0);
    output.tbn_valid = 0.0;
    output.color = input.color;
    return output;
}

[shader("fragment")]
FragmentOutput fs_main(TubeOutput input) {
    FragmentOutput output;
    output.color = input.color;
    return output;
}

float3 apply_tube_lighting(float3 base_color, float3 normal, float3 world_pos) {
    float3 n = normalize(normal);
    float3 result = base_color * get_ambient_color() * get_ambient_intensity();
    int count = min(get_light_count(), MAX_LIGHTS);
    for (int i = 0; i < count; ++i) {
        int type = get_light_type(i);
        float3 l;
        float weight = 1.0;
        if (type == LIGHT_TYPE_DIRECTIONAL) {
            l = normalize(-get_light_direction(i));
        } else {
            float3 to_light = get_light_position(i) - world_pos;
            float dist = length(to_light);
            l = dist > 1.0e-4 ? to_light / dist : float3(0.0, 1.0, 0.0);
            weight *= compute_distance_attenuation(
                get_light_attenuation(i),
                get_light_range(i),
                dist);
            if (type == LIGHT_TYPE_SPOT) {
                weight *= compute_spot_weight(
                    get_light_direction(i),
                    l,
                    get_light_inner_angle(i),
                    get_light_outer_angle(i));
            }
        }
        float ndotl = abs(dot(n, l));
        result += base_color * get_light_color(i) * get_light_intensity(i) * ndotl * weight;
    }
    return result;
}

[shader("fragment")]
FragmentOutput fs_lit_main(TubeOutput input) {
    FragmentOutput output;
    output.color = float4(
        apply_tube_lighting(input.color.rgb, input.normal_world, input.world_pos),
        input.color.a);
    return output;
}
