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

struct DepthMaterialParams {
    float u_near;
    float u_far;
    float2 _pad;
};

[[TerminScope("material")]]
ConstantBuffer<DepthMaterialParams> material;

struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texcoord : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float linear_depth : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    float4 world_pos = mul(draw_data.model, float4(input.position, 1.0));
    float4 view_pos = mul(per_frame.u_view, world_pos);

    float denom = max(material.u_far - material.u_near, 1.0e-6);
    output.linear_depth = (view_pos.y - material.u_near) / denom;
    output.position = mul(per_frame.u_projection, view_pos);
    return output;
}

struct FragmentInput {
    float linear_depth : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    FragmentOutput output;
    float d = clamp(input.linear_depth, 0.0, 1.0);
    output.color = float4(d, 0.0, 0.0, 1.0);
    return output;
}
