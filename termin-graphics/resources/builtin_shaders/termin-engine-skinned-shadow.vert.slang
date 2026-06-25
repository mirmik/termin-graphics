import termin_prelude;
import termin_vertex_transform;

struct PerFrame {
    column_major float4x4 u_view;
    column_major float4x4 u_projection;
};

[[TerminScope("frame")]]
ConstantBuffer<PerFrame> per_frame;

struct ShadowPushData {
    column_major float4x4 u_model;
};

[[TerminScope("draw")]]
ConstantBuffer<ShadowPushData> shadow_draw;

[[TerminScope("draw")]]
ConstantBuffer<TerminBoneBlock> bone_block;

struct VertexInput {
    float3 position : POSITION;
    float4 joints : TEXCOORD0;
    float4 weights : TEXCOORD1;
};

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    float3 local_pos =
        termin_skinned_position(input.position, input.joints, input.weights, bone_block);
    float4 world_pos = mul(shadow_draw.u_model, float4(local_pos, 1.0));
    output.position = termin_to_native_clip(mul(per_frame.u_projection, mul(per_frame.u_view, world_pos)));
    return output;
}
