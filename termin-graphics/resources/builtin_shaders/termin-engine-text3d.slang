import termin_prelude;

struct Text3DPush {
    column_major float4x4 u_mvp;
    float4 u_color;
    float4 u_cam_right;
    float4 u_cam_up;
};

[[TerminScope("draw")]]
ConstantBuffer<Text3DPush> text3d_draw;

[[TerminScope("transient")]]
Sampler2D u_font_atlas;

struct VertexInput {
    float3 world_pos : POSITION;
    float4 offset_uv : TEXCOORD0;
};

struct VertexOutput {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    float3 pos =
        input.world_pos +
        text3d_draw.u_cam_right.xyz * input.offset_uv.x +
        text3d_draw.u_cam_up.xyz * input.offset_uv.y;
    output.pos = termin_to_native_clip(mul(text3d_draw.u_mvp, float4(pos, 1.0)));
    output.uv = input.offset_uv.zw;
    return output;
}

struct FragmentInput {
    float4 screen_pos : SV_Position;
    float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    FragmentOutput output;
    float a = u_font_atlas.Sample(input.uv).r * text3d_draw.u_color.a;
    if (a < (1.0 / 255.0)) {
        discard;
    }
    output.color = float4(text3d_draw.u_color.rgb, a);
    return output;
}
