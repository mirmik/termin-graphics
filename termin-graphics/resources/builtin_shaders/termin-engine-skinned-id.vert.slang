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
ConstantBuffer<PerFrame> per_frame;

[[TerminScope("draw")]]
ConstantBuffer<PushData> id_draw;

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
    output.position = termin_to_native_clip(mul(per_frame.projection,
                     mul(per_frame.view,
                     mul(id_draw.model, float4(local_pos, 1.0)))));
    return output;
}
