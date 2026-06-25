import termin_prelude;
import termin_lighting;

struct WorldLineDraw {
    float4x4 u_view_projection;
    float4 u_camera_position;
};

[[TerminScope("draw")]]
ConstantBuffer<WorldLineDraw> world_line_draw;

struct SegmentInput {
    float2 corner : TEXCOORD0;
    float3 p0 : POSITION0;
    float width : TEXCOORD1;
    float3 p1 : POSITION1;
    float flags : TEXCOORD2;
    float4 color : COLOR0;
};

struct CapInput {
    float2 local : TEXCOORD0;
    float3 center : POSITION0;
    float width : TEXCOORD1;
    float3 neighbor : POSITION1;
    float flags : TEXCOORD2;
    float4 color : COLOR0;
};

struct JoinInput {
    float corner : TEXCOORD0;
    float3 prev : POSITION0;
    float width : TEXCOORD1;
    float3 center : POSITION1;
    float flags : TEXCOORD2;
    float3 next : POSITION2;
    float4 color : COLOR0;
};

struct LineOutput {
    float4 position : SV_Position;
    float3 world_pos : POSITION1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
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

float3 billboard_side(float3 segment_dir, float3 point) {
    float3 to_eye = safe_normalize(
        world_line_draw.u_camera_position.xyz - point,
        float3(0.0, 0.0, 1.0));
    float3 side = cross(segment_dir, to_eye);
    float len = length(side);
    if (len >= 1.0e-6) {
        return side / len;
    }

    side = cross(segment_dir, float3(0.0, 0.0, 1.0));
    len = length(side);
    if (len >= 1.0e-6) {
        return side / len;
    }

    return safe_normalize(
        cross(segment_dir, float3(0.0, 1.0, 0.0)),
        float3(1.0, 0.0, 0.0));
}

[shader("vertex")]
LineOutput vs_main(SegmentInput input) {
    float endpoint = input.corner.x;
    float side_sign = input.corner.y;
    float3 dir = safe_normalize(input.p1 - input.p0, float3(1.0, 0.0, 0.0));
    float3 base = lerp(input.p0, input.p1, endpoint);

    bool extend_start = fmod(input.flags, 2.0) >= 1.0;
    bool extend_end = input.flags >= 2.0;
    if (endpoint < 0.5 && extend_start) {
        base -= dir * (input.width * 0.5);
    } else if (endpoint >= 0.5 && extend_end) {
        base += dir * (input.width * 0.5);
    }

    float3 mid = (input.p0 + input.p1) * 0.5;
    float3 side = billboard_side(dir, mid);
    float3 expanded = base + side * side_sign * (input.width * 0.5);

    LineOutput output;
    output.position = termin_to_native_clip(mul(world_line_draw.u_view_projection, float4(expanded, 1.0)));
    output.world_pos = expanded;
    output.normal = safe_normalize(
        world_line_draw.u_camera_position.xyz - mid,
        float3(0.0, 0.0, 1.0));
    output.uv = float2(endpoint, side_sign * 0.5 + 0.5);
    output.color = input.color;
    return output;
}

[shader("vertex")]
LineOutput vs_cap_main(CapInput input) {
    float3 dir = safe_normalize(input.center - input.neighbor, float3(1.0, 0.0, 0.0));
    float3 side = billboard_side(dir, input.center);
    float3 expanded = input.center
        + dir * input.local.x * (input.width * 0.5)
        + side * input.local.y * (input.width * 0.5);

    LineOutput output;
    output.position = termin_to_native_clip(mul(world_line_draw.u_view_projection, float4(expanded, 1.0)));
    output.world_pos = expanded;
    output.normal = safe_normalize(
        world_line_draw.u_camera_position.xyz - input.center,
        float3(0.0, 0.0, 1.0));
    output.uv = input.local * 0.5 + 0.5;
    output.color = input.color;
    return output;
}

[shader("vertex")]
LineOutput vs_join_main(JoinInput input) {
    float3 d0 = safe_normalize(input.center - input.prev, float3(1.0, 0.0, 0.0));
    float3 d1 = safe_normalize(input.next - input.center, float3(1.0, 0.0, 0.0));
    float3 to_eye = safe_normalize(
        world_line_draw.u_camera_position.xyz - input.center,
        float3(0.0, 0.0, 1.0));
    float side_sign = dot(cross(d0, d1), to_eye) >= 0.0 ? 1.0 : -1.0;
    if (length(cross(d0, d1)) < 1.0e-6) {
        side_sign = 0.0;
    }

    float3 side0 = billboard_side(d0, input.center);
    float3 side1 = billboard_side(d1, input.center);
    float3 p = input.center;
    if (input.corner > 0.5 && input.corner < 1.5) {
        p = input.center + side0 * side_sign * (input.width * 0.5);
    } else if (input.corner >= 1.5) {
        p = input.center + side1 * side_sign * (input.width * 0.5);
    }

    LineOutput output;
    output.position = termin_to_native_clip(mul(world_line_draw.u_view_projection, float4(p, 1.0)));
    output.world_pos = p;
    output.normal = to_eye;
    output.uv = float2(input.corner * 0.5, side_sign * 0.5 + 0.5);
    output.color = input.color;
    return output;
}

[shader("vertex")]
LineOutput vs_round_join_main(CapInput input) {
    float3 dir = safe_normalize(input.neighbor - input.center, float3(1.0, 0.0, 0.0));
    float3 to_eye = safe_normalize(
        world_line_draw.u_camera_position.xyz - input.center,
        float3(0.0, 0.0, 1.0));
    float3 axis0 = billboard_side(dir, input.center);
    float3 axis1 = safe_normalize(cross(to_eye, axis0), dir);
    float3 expanded = input.center
        + axis0 * input.local.x * (input.width * 0.5)
        + axis1 * input.local.y * (input.width * 0.5);

    LineOutput output;
    output.position = termin_to_native_clip(mul(world_line_draw.u_view_projection, float4(expanded, 1.0)));
    output.world_pos = expanded;
    output.normal = to_eye;
    output.uv = input.local * 0.5 + 0.5;
    output.color = input.color;
    return output;
}

[shader("fragment")]
FragmentOutput fs_main(LineOutput input) {
    FragmentOutput output;
    output.color = input.color;
    return output;
}

float3 apply_line_lighting(float3 base_color, float3 normal, float3 world_pos) {
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
FragmentOutput fs_lit_main(LineOutput input) {
    FragmentOutput output;
    output.color = float4(
        apply_line_lighting(input.color.rgb, input.normal, input.world_pos),
        input.color.a);
    return output;
}
