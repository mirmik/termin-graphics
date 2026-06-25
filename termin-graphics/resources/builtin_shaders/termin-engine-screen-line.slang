import termin_prelude;

struct ScreenLineDraw {
    float4x4 u_view_projection;
    float4 u_viewport;
};

[[TerminScope("draw")]]
ConstantBuffer<ScreenLineDraw> screen_line_draw;

struct SegmentInput {
    float2 corner : TEXCOORD0;
    float3 p0 : POSITION0;
    float width_px : TEXCOORD1;
    float3 p1 : POSITION1;
    float flags : TEXCOORD2;
    float4 color : COLOR0;
};

struct CapInput {
    float2 local : TEXCOORD0;
    float3 center : POSITION0;
    float width_px : TEXCOORD1;
    float3 neighbor : POSITION1;
    float flags : TEXCOORD2;
    float4 color : COLOR0;
};

struct JoinInput {
    float corner : TEXCOORD0;
    float3 prev : POSITION0;
    float width_px : TEXCOORD1;
    float3 center : POSITION1;
    float flags : TEXCOORD2;
    float3 next : POSITION2;
    float4 color : COLOR0;
};

struct LineOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

float2 screen_line_viewport() {
    return max(screen_line_draw.u_viewport.xy, float2(1.0, 1.0));
}

float2 clip_to_px(float4 clip, float2 viewport) {
    float2 ndc = clip.xy / clip.w;
    return (ndc * 0.5 + 0.5) * viewport;
}

[shader("vertex")]
LineOutput vs_main(SegmentInput input) {
    float4 c0 = mul(screen_line_draw.u_view_projection, float4(input.p0, 1.0));
    float4 c1 = mul(screen_line_draw.u_view_projection, float4(input.p1, 1.0));

    float endpoint = input.corner.x;
    float side_sign = input.corner.y;

    float2 viewport = screen_line_viewport();
    float2 px0 = clip_to_px(c0, viewport);
    float2 px1 = clip_to_px(c1, viewport);

    float2 dir = px1 - px0;
    float len = length(dir);
    if (len < 1.0e-5) {
        dir = float2(1.0, 0.0);
    } else {
        dir /= len;
    }

    float2 side = float2(-dir.y, dir.x);
    float2 base_px = lerp(px0, px1, endpoint);
    bool extend_start = fmod(input.flags, 2.0) >= 1.0;
    bool extend_end = input.flags >= 2.0;
    if (endpoint < 0.5 && extend_start) {
        base_px -= dir * (input.width_px * 0.5);
    } else if (endpoint >= 0.5 && extend_end) {
        base_px += dir * (input.width_px * 0.5);
    }
    float2 expanded_px = base_px + side * side_sign * (input.width_px * 0.5);
    float2 expanded_ndc = expanded_px / viewport * 2.0 - 1.0;

    float4 clip = lerp(c0, c1, endpoint);
    clip.xy = expanded_ndc * clip.w;

    LineOutput output;
    output.position = termin_to_native_clip(clip);
    output.color = input.color;
    return output;
}

[shader("vertex")]
LineOutput vs_cap_main(CapInput input) {
    float4 c0 = mul(screen_line_draw.u_view_projection, float4(input.center, 1.0));
    float4 c1 = mul(screen_line_draw.u_view_projection, float4(input.neighbor, 1.0));

    float2 viewport = screen_line_viewport();
    float2 px0 = clip_to_px(c0, viewport);
    float2 px1 = clip_to_px(c1, viewport);

    float2 dir = px0 - px1;
    float len = length(dir);
    if (len < 1.0e-5) {
        dir = float2(1.0, 0.0);
    } else {
        dir /= len;
    }

    float2 side = float2(-dir.y, dir.x);
    float2 expanded_px = px0
        + dir * input.local.x * (input.width_px * 0.5)
        + side * input.local.y * (input.width_px * 0.5);
    float2 expanded_ndc = expanded_px / viewport * 2.0 - 1.0;

    float4 clip = c0;
    clip.xy = expanded_ndc * clip.w;

    LineOutput output;
    output.position = termin_to_native_clip(clip);
    output.color = input.color;
    return output;
}

[shader("vertex")]
LineOutput vs_join_main(JoinInput input) {
    float4 cp = mul(screen_line_draw.u_view_projection, float4(input.prev, 1.0));
    float4 cc = mul(screen_line_draw.u_view_projection, float4(input.center, 1.0));
    float4 cn = mul(screen_line_draw.u_view_projection, float4(input.next, 1.0));

    float2 viewport = screen_line_viewport();
    float2 px_prev = clip_to_px(cp, viewport);
    float2 px_center = clip_to_px(cc, viewport);
    float2 px_next = clip_to_px(cn, viewport);

    float2 d0 = px_center - px_prev;
    float2 d1 = px_next - px_center;
    float len0 = length(d0);
    float len1 = length(d1);
    LineOutput output;
    output.color = input.color;
    if (len0 < 1.0e-5 || len1 < 1.0e-5) {
        output.position = termin_to_native_clip(cc);
        return output;
    }
    d0 /= len0;
    d1 /= len1;

    float cross_z = d0.x * d1.y - d0.y * d1.x;
    float side_sign = cross_z >= 0.0 ? 1.0 : -1.0;
    if (abs(cross_z) < 1.0e-5) {
        side_sign = 0.0;
    }

    float2 side0 = float2(-d0.y, d0.x);
    float2 side1 = float2(-d1.y, d1.x);
    float2 p = px_center;
    if (input.corner > 0.5 && input.corner < 1.5) {
        p = px_center + side0 * side_sign * (input.width_px * 0.5);
    } else if (input.corner >= 1.5) {
        p = px_center + side1 * side_sign * (input.width_px * 0.5);
    }

    float2 ndc = p / viewport * 2.0 - 1.0;
    float4 clip = cc;
    clip.xy = ndc * clip.w;
    output.position = termin_to_native_clip(clip);
    return output;
}

[shader("vertex")]
LineOutput vs_round_join_main(CapInput input) {
    float4 cc = mul(screen_line_draw.u_view_projection, float4(input.center, 1.0));

    float2 viewport = screen_line_viewport();
    float2 px_center = clip_to_px(cc, viewport);
    float2 expanded_px = px_center + input.local * (input.width_px * 0.5);
    float2 expanded_ndc = expanded_px / viewport * 2.0 - 1.0;

    float4 clip = cc;
    clip.xy = expanded_ndc * clip.w;

    LineOutput output;
    output.position = termin_to_native_clip(clip);
    output.color = input.color;
    return output;
}

[shader("fragment")]
FragmentOutput fs_main(LineOutput input) {
    FragmentOutput output;
    output.color = input.color;
    return output;
}
