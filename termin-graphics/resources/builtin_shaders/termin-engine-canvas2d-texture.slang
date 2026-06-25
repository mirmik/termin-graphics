import termin_prelude;

struct CanvasPushData {
    column_major float4x4 u_projection;
    float4 u_color;
};

[[TerminScope("draw")]]
ConstantBuffer<CanvasPushData> canvas_draw;

[[TerminScope("transient")]]
Sampler2D u_texture;

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
    output.pos = termin_to_native_clip(mul(canvas_draw.u_projection, float4(input.pos.xy, 0.0, 1.0)));
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
    output.color = u_texture.Sample(input.uv) * canvas_draw.u_color;
    return output;
}
