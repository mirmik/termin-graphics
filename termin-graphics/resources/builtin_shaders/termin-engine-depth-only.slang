import termin_prelude;

struct PerFrame {
    column_major float4x4 u_view;
    column_major float4x4 u_projection;
    float u_near;
    float u_far;
};

[[TerminScope("frame")]]
ConstantBuffer<PerFrame> per_frame;

struct DepthPushData {
    column_major float4x4 u_model;
};

[[TerminScope("draw")]]
ConstantBuffer<DepthPushData> depth_draw;

struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    float4 world_pos = mul(depth_draw.u_model, float4(input.position, 1.0));
    float4 view_pos = mul(per_frame.u_view, world_pos);
    output.position = termin_to_native_clip(mul(per_frame.u_projection, view_pos));
    return output;
}

[shader("fragment")]
void fs_main() {
}
