@program Skybox
@language slang

@phase opaque
@priority 0
@glDepthTest true
@glDepthMask false
@glCull false

@property Mat4  u_view
@property Mat4  u_projection
@property Int   u_skybox_type
@property Color u_skybox_color        = Color(0.5, 0.5, 0.5, 1.0)
@property Color u_skybox_top_color    = Color(0.3, 0.5, 1.0, 1.0)
@property Color u_skybox_bottom_color = Color(0.1, 0.1, 0.3, 1.0)

@stage vertex
struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 dir : TEXCOORD0;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    VertexOutput output;
    float4 view_dir = mul(material.u_view, float4(input.position, 0.0));
    output.dir = input.position;
    output.position = mul(material.u_projection, float4(view_dir.xyz, 1.0));
    return output;
}
@endstage

@stage fragment
struct FragmentInput {
    float3 dir : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input) {
    FragmentOutput output;
    if (material.u_skybox_type == 1) {
        output.color = float4(material.u_skybox_color.rgb, 1.0);
    } else {
        float t = normalize(input.dir).z * 0.5 + 0.5;
        float3 color = lerp(
            material.u_skybox_bottom_color.rgb,
            material.u_skybox_top_color.rgb,
            t);
        output.color = float4(color, 1.0);
    }
    return output;
}
@endstage

@endphase
