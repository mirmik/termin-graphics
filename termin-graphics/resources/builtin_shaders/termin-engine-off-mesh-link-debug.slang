import termin_prelude;

struct PerFrame {
    column_major float4x4 u_view;
    column_major float4x4 u_projection;
    column_major float4x4 u_view_projection;
    column_major float4x4 u_inv_view;
    column_major float4x4 u_inv_proj;
    float4 u_camera_position;
    float2 u_resolution;
    float u_near;
    float u_far;
};

[[TerminScope("frame")]]
ConstantBuffer<PerFrame> per_frame;

struct DrawData {
    column_major float4x4 model;
};

[[TerminScope("draw")]]
ConstantBuffer<DrawData> draw_data;

struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.position = termin_to_native_clip(mul(per_frame.u_view_projection,
                          mul(draw_data.model, float4(input.position, 1.0))));
    return output;
}

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main() {
    FragmentOutput output;
    output.color = float4(1.0, 0.55, 0.08, 0.9);
    return output;
}
