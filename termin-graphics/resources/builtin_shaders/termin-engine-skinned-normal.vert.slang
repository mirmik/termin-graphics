import termin_prelude;
import termin_vertex_transform;

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

[[TerminScope("draw")]]
ConstantBuffer<TerminBoneBlock> bone_block;

struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 joints : TEXCOORD0;
    float4 weights : TEXCOORD1;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 world_normal : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    float3 local_pos =
        termin_skinned_position(input.position, input.joints, input.weights, bone_block);
    float3 local_normal =
        termin_skinned_normal(input.normal, input.joints, input.weights, bone_block);

    float3x3 normal_matrix = (float3x3)normal_draw.u_model;
    output.world_normal = normalize(mul(normal_matrix, local_normal));

    float4 world_pos = mul(normal_draw.u_model, float4(local_pos, 1.0));
    output.position = mul(per_frame.u_projection, mul(per_frame.u_view, world_pos));
    return output;
}
