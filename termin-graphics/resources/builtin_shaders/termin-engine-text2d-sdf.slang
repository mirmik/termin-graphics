import termin_prelude;

struct Text2DSdfPushData {
    column_major float4x4 u_projection;
    float4 u_color;
    float u_smoothing;
};

[[TerminScope("draw")]]
ConstantBuffer<Text2DSdfPushData> text2d_sdf_draw;

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
    output.pos = mul(text2d_sdf_draw.u_projection, float4(input.pos.xy, 0.0, 1.0));
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
    float d = u_font_atlas.Sample(input.uv).r;
    float a = smoothstep(
        0.5 - text2d_sdf_draw.u_smoothing,
        0.5 + text2d_sdf_draw.u_smoothing,
        d) * text2d_sdf_draw.u_color.a;
    if (a < (1.0 / 255.0)) {
        discard;
    }
    output.color = float4(text2d_sdf_draw.u_color.rgb, a);
    return output;
}
