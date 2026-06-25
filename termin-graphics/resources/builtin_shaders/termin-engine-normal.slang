import termin_prelude;

struct PerFrame {
    column_major float4x4 u_view;
    column_major float4x4 u_projection;
};

[[TerminScope("frame")]]
ConstantBuffer<PerFrame> per_frame;

struct NormalPushData {
    column_major float4x4 u_model;
};

[[TerminScope("draw")]]
ConstantBuffer<NormalPushData> normal_draw;

struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 world_normal : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    float3x3 normal_matrix = (float3x3)normal_draw.u_model;
    output.world_normal = normalize(mul(normal_matrix, input.normal));

    float4 world_pos = mul(normal_draw.u_model, float4(input.position, 1.0));
    output.position = termin_to_native_clip(mul(per_frame.u_projection, mul(per_frame.u_view, world_pos)));
    return output;
}

struct FragmentInput {
    float4 screen_pos : SV_Position;
    float3 world_normal : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    FragmentOutput output;
    float3 encoded = normalize(input.world_normal) * 0.5 + 0.5;
    output.color = float4(encoded, 1.0);
    return output;
}
