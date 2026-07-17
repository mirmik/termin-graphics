// Picking pass projection output.

import termin_prelude;

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

struct VertexOutput {
    float4 position : SV_Position;
};

VertexOutput termin_id_vertex_output(float3 world_pos) {
    VertexOutput output;
    output.position = termin_to_native_clip(
        mul(per_frame.projection, mul(per_frame.view, float4(world_pos, 1.0))));
    return output;
}
