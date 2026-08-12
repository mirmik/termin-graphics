import termin_prelude;

struct StaticMeshTexturedPush {
    column_major float4x4 u_view_projection;
    float4 u_base_color_factor;
};

[[TerminScope("draw")]]
ConstantBuffer<StaticMeshTexturedPush> u_push;

[[TerminScope("transient")]]
Sampler2D u_base_color_texture;

struct VertexInput {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.position = termin_to_native_clip(
        mul(u_push.u_view_projection, float4(input.position, 1.0)));
    output.uv = input.uv;
    return output;
}

struct FragmentInput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    FragmentOutput output;
    output.color = u_base_color_texture.Sample(input.uv) *
                   u_push.u_base_color_factor;
    return output;
}
