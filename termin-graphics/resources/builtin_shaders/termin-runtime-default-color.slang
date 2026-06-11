import termin_prelude;

struct CameraData {
    column_major float4x4 view;
    column_major float4x4 proj;
    column_major float4x4 view_proj;
    float4 camera_pos;
};

[[TerminScope("frame")]]
ConstantBuffer<CameraData> CameraUBO;

struct PushConstantsData {
    column_major float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<PushConstantsData> PushConstants;

struct VertexInput {
    float3 position : POSITION;
    float3 color : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.color = input.color;
    output.position =
        mul(CameraUBO.view_proj, mul(PushConstants.model, float4(input.position, 1.0)));
    return output;
}

struct FragmentInput {
    float3 color : COLOR0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    FragmentOutput output;
    output.color = float4(input.color, 1.0);
    return output;
}
