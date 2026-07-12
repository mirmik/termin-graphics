// Pass-owned shadow vertex-output adapter.  It consumes a world-space
// position from a transform provider and emits native clip-space output.

import termin_prelude;

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

struct VertexOutput {
    float4 position : SV_Position;
};

VertexOutput termin_shadow_clip_output(float3 world_pos) {
    VertexOutput output;
    output.position = termin_to_native_clip(
        mul(per_frame.u_projection, mul(per_frame.u_view, float4(world_pos, 1.0))));
    return output;
}
