// Cross-backend point clouds rendered as instanced screen-space quads.

import termin_prelude;

struct PointCloudDraw {
    float4x4 view_projection;
    // xy: viewport pixels, z: base point diameter in pixels, w: circle flag.
    float4 viewport_size_shape;
    float4 tint;
};

[[TerminScope("draw")]]
ConstantBuffer<PointCloudDraw> point_cloud_draw;

struct PointInput {
    float2 local : TEXCOORD0;
    float3 center : POSITION0;
    float size_scale : TEXCOORD1;
    float4 color : COLOR0;
};

struct PointOutput {
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
PointOutput vs_main(PointInput input) {
    float4 clip = mul(point_cloud_draw.view_projection, float4(input.center, 1.0));
    float2 viewport = max(point_cloud_draw.viewport_size_shape.xy, float2(1.0, 1.0));
    float diameter_px = max(point_cloud_draw.viewport_size_shape.z * input.size_scale, 0.0);
    clip.xy += input.local * (diameter_px / viewport) * clip.w;

    PointOutput output;
    output.position = termin_to_native_clip(clip);
    output.local = input.local;
    output.color = input.color * point_cloud_draw.tint;
    return output;
}

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(PointOutput input) {
    if (point_cloud_draw.viewport_size_shape.w > 0.5 && dot(input.local, input.local) > 1.0) {
        discard;
    }
    FragmentOutput output;
    output.color = input.color;
    return output;
}
