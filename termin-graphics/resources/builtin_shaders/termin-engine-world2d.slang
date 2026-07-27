import termin_prelude;

struct World2DFrame {
    column_major float4x4 view_projection;
};

[[TerminScope("draw")]]
ConstantBuffer<World2DFrame> world2d_frame;

[[TerminScope("transient")]]
Sampler2D u_texture;

struct VertexInput {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 tint : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 tint : COLOR0;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.position = termin_to_native_clip(
        mul(world2d_frame.view_projection, float4(input.position, 1.0)));
    output.uv = input.uv;
    output.tint = input.tint;
    return output;
}

struct FragmentInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 tint : COLOR0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    FragmentOutput output;
    output.color = u_texture.Sample(input.uv) * input.tint;
    return output;
}
