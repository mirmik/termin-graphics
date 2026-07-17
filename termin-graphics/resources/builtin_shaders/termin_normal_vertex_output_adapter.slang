// World-normal pass projection and output.

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

struct VertexOutput {
    float4 position : SV_Position;
    float3 world_normal : TEXCOORD0;
};

VertexOutput termin_normal_vertex_output(TerminWorldPositionNormal world) {
    VertexOutput output;
    output.world_normal = normalize(world.normal);
    output.position = termin_to_native_clip(
        mul(per_frame.u_projection,
            mul(per_frame.u_view, float4(world.position, 1.0))));
    return output;
}
