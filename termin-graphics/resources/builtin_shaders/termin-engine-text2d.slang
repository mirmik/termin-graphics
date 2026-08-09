import termin_prelude;

struct Text2DPushData {
    column_major float4x4 u_projection;
    float4 u_color;
    float u_coverage_gamma;
};

[[TerminScope("draw")]]
ConstantBuffer<Text2DPushData> text2d_draw;

[[TerminScope("transient")]]
Sampler2D u_font_atlas;

struct VertexInput {
    float3 pos : POSITION;
    float4 uv_pad : TEXCOORD0;
};

struct VertexOutput {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.pos = termin_to_native_clip(mul(text2d_draw.u_projection, float4(input.pos.xy, 0.0, 1.0)));
    output.uv = input.uv_pad.xy;
    return output;
}

struct FragmentInput {
    float4 screen_pos : SV_Position;
    float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    FragmentOutput output;
    float coverage = saturate(u_font_atlas.Sample(input.uv).r);
    // Grayscale glyph masks describe geometric coverage. When their edge
    // values are blended directly into a linear target, small light glyphs
    // look heavier than the rasterised outline. Keep solid stem pixels intact
    // while gently reducing only the partially covered bitmap fringe.
    coverage = pow(coverage, text2d_draw.u_coverage_gamma);
    float a = coverage * text2d_draw.u_color.a;
    if (a < (1.0 / 255.0)) {
        discard;
    }
    output.color = float4(text2d_draw.u_color.rgb, a);
    return output;
}
