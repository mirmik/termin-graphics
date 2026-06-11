import termin_prelude;

struct GizmoPushData {
    column_major float4x4 u_mvp;
    float4 u_color;
};

[[TerminScope("draw")]]
ConstantBuffer<GizmoPushData> gizmo_push;

struct VertexInput {
    float3 pos : POSITION;
};

struct VertexOutput {
    float4 pos : SV_Position;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.pos = mul(gizmo_push.u_mvp, float4(input.pos, 1.0));
    return output;
}

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main() {
    FragmentOutput output;
    output.color = float4(0.0, 0.0, 0.0, gizmo_push.u_color.a);
    return output;
}
