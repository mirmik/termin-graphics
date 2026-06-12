import termin_prelude;
import termin_vertex_transform;

struct PerFrame {
    float4x4 view;
    float4x4 projection;
};

struct PushData {
    float4x4 model;
    float4 pick_color;
};

[[TerminScope("frame")]]
ConstantBuffer<PerFrame> u_per_frame;

[[TerminScope("draw")]]
ConstantBuffer<PushData> u_push;

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
    output.position = mul(u_per_frame.projection,
                     mul(u_per_frame.view,
                     mul(u_push.model, float4(local_pos, 1.0))));
    return output;
}
