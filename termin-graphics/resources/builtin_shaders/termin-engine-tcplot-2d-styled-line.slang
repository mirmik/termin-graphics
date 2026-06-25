import termin_prelude;

struct Tcplot2DStyledLineDraw {
    column_major float4x4 u_matrix;
    float4 u_color;
    float4 u_params;    // thickness_px, style_id, colormap_id, colormap_reversed
    float4 u_range;     // scalar_min, scalar_max, length_scale, unused
    float4 u_viewport;  // width, height, dash_px, gap_px
};

[[TerminScope("draw")]]
ConstantBuffer<Tcplot2DStyledLineDraw> tcplot2d_styled_line_draw;

struct VertexInput {
    float2 prev : LINEPREV;
    float2 curr : LINECURR;
    float2 next : LINENEXT;
    float4 meta : LINEMETA; // side, scalar, cumulative_length, unused
};

struct VertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float4 meta : TEXCOORD0; // cumulative_length, style_id, dash_px, gap_px
};

float2 clip_to_px(float4 c) {
    float2 ndc = c.xy / c.w;
    return (ndc * 0.5 + 0.5) * tcplot2d_styled_line_draw.u_viewport.xy;
}

float3 jet(float t) {
    t = clamp(t, 0.0, 1.0);
    float r = clamp(1.5 - abs(4.0 * t - 3.0), 0.0, 1.0);
    float g = clamp(1.5 - abs(4.0 * t - 2.0), 0.0, 1.0);
    float b = clamp(1.5 - abs(4.0 * t - 1.0), 0.0, 1.0);
    return float3(r, g, b);
}

float3 viridis(float t) {
    float3 c0 = float3(0.277, 0.005, 0.334);
    float3 c1 = float3(0.128, 0.567, 0.551);
    float3 c2 = float3(0.741, 0.873, 0.150);
    return t < 0.5 ? lerp(c0, c1, t * 2.0) : lerp(c1, c2, (t - 0.5) * 2.0);
}

float3 plasma(float t) {
    float3 c0 = float3(0.050, 0.030, 0.528);
    float3 c1 = float3(0.798, 0.280, 0.470);
    float3 c2 = float3(0.940, 0.975, 0.131);
    return t < 0.5 ? lerp(c0, c1, t * 2.0) : lerp(c1, c2, (t - 0.5) * 2.0);
}

float3 colormap(float id, float t, float3 fallback) {
    if (id < 0.5) return jet(t);
    if (id < 1.5) return viridis(t);
    if (id < 2.5) return plasma(t);
    if (id < 3.5) return float3(t);
    if (id < 4.5) {
        return float3(t, 0.25 + 0.5 * (1.0 - abs(2.0 * t - 1.0)), 1.0 - t);
    }
    return fallback;
}

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    float4 cp = mul(tcplot2d_styled_line_draw.u_matrix, float4(input.prev, 0.0, 1.0));
    float4 cc = mul(tcplot2d_styled_line_draw.u_matrix, float4(input.curr, 0.0, 1.0));
    float4 cn = mul(tcplot2d_styled_line_draw.u_matrix, float4(input.next, 0.0, 1.0));

    float2 pp = clip_to_px(cp);
    float2 pcurr = clip_to_px(cc);
    float2 pn = clip_to_px(cn);
    float2 dir = pn - pp;
    float len = length(dir);
    if (len < 0.001) {
        dir = float2(1.0, 0.0);
    } else {
        dir /= len;
    }
    float2 normal = float2(-dir.y, dir.x);

    float2 px = pcurr + normal * input.meta.x * tcplot2d_styled_line_draw.u_params.x * 0.5;
    float2 ndc = px / tcplot2d_styled_line_draw.u_viewport.xy * 2.0 - 1.0;
    output.position = float4(ndc * cc.w, cc.z, cc.w);

    output.color = tcplot2d_styled_line_draw.u_color;
    output.meta = float4(input.meta.z * tcplot2d_styled_line_draw.u_range.z,
                         tcplot2d_styled_line_draw.u_params.y,
                         tcplot2d_styled_line_draw.u_viewport.z,
                         tcplot2d_styled_line_draw.u_viewport.w);
    output.meta.x = max(output.meta.x, 0.0);
    output.color.a *= tcplot2d_styled_line_draw.u_params.w;
    output.color.r = input.meta.y;
    return output;
}

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(VertexOutput input) {
    FragmentOutput output;
    float style_id = input.meta.y;
    if (style_id > 0.5) {
        float dash_px = max(input.meta.z, 1.0);
        float gap_px = max(input.meta.w, 1.0);
        float period = dash_px + gap_px;
        float p = fmod(input.meta.x, period);
        if (p > dash_px) discard;
        if (style_id > 1.5 && p > min(dash_px, tcplot2d_styled_line_draw.u_params.x)) discard;
    }

    float4 c = tcplot2d_styled_line_draw.u_color;
    if (tcplot2d_styled_line_draw.u_params.z < 5.5) {
        float denom = max(tcplot2d_styled_line_draw.u_range.y -
                          tcplot2d_styled_line_draw.u_range.x, 1e-12);
        float t = clamp((input.color.r - tcplot2d_styled_line_draw.u_range.x) / denom,
                        0.0, 1.0);
        if (tcplot2d_styled_line_draw.u_params.w != 0.0) {
            t = 1.0 - t;
        }
        c.rgb = colormap(tcplot2d_styled_line_draw.u_params.z, t, input.color.rgb);
    }
    output.color = c;
    return output;
}
