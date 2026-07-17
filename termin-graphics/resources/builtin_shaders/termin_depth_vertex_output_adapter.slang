// Depth pass projection and encoded-depth output.

import termin_prelude;

struct PerFrame {
    column_major float4x4 u_view;
    column_major float4x4 u_projection;
    float u_near;
    float u_far;
    float u_depth_encoding;
};

[[TerminScope("frame")]]
ConstantBuffer<PerFrame> per_frame;

struct DepthPushData {
    column_major float4x4 u_model;
};

[[TerminScope("draw")]]
ConstantBuffer<DepthPushData> depth_draw;

struct VertexOutput {
    float4 position : SV_Position;
    float linear_depth : TEXCOORD0;
    float perspective_depth : TEXCOORD1;
    float log_depth : TEXCOORD2;
};

VertexOutput termin_depth_vertex_output(float3 world_pos) {
    VertexOutput output;
    float4 view_pos = mul(per_frame.u_view, float4(world_pos, 1.0));
    float view_depth = view_pos.y;
    float depth =
        (view_depth - per_frame.u_near) / (per_frame.u_far - per_frame.u_near);
    float4 clip_pos = mul(per_frame.u_projection, view_pos);
    float ndc_depth = clip_pos.z / max(abs(clip_pos.w), 1.0e-6);

    output.position = termin_to_native_clip(clip_pos);
    output.linear_depth = depth;
    output.perspective_depth = ndc_depth;
    output.log_depth =
        log2(max(view_depth, 0.0) + 1.0) /
        log2(max(per_frame.u_far, 1.0e-6) + 1.0);
    return output;
}
