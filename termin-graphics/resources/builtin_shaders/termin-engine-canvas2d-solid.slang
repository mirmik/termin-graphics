import termin_prelude;

struct CanvasPushData {
    column_major float4x4 u_projection;
    float4 u_color;
};

[[TerminScope("draw")]]
ConstantBuffer<CanvasPushData> canvas_draw;

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
    output.pos = mul(canvas_draw.u_projection, float4(input.pos.xy, 0.0, 1.0));
    output.uv = input.uv_pad.xy;
    return output;
}

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main() {
    FragmentOutput output;
    output.color = canvas_draw.u_color;
    return output;
}
