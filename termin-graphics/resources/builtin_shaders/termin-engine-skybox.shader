@program Skybox
@language slang

@phase opaque
@priority 0
@glDepthTest true
@glDepthMask false
@glCull false

@property Mat4  u_inv_view_projection
@property Int   u_skybox_type
@property SrgbColor u_skybox_color        = SrgbColor(0.5, 0.5, 0.5, 1.0)
@property SrgbColor u_skybox_top_color    = SrgbColor(0.4, 0.6, 0.9, 1.0)
@property SrgbColor u_skybox_horizon_color = SrgbColor(0.5, 0.55, 0.65, 1.0)
@property SrgbColor u_skybox_bottom_color = SrgbColor(0.6, 0.5, 0.4, 1.0)
@property Float u_skybox_top_exponent = 1.0
@property Float u_skybox_bottom_exponent = 1.0

@stage vertex
import termin_prelude;

struct VertexInput {
    float2 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 dir : TEXCOORD0;
};

[shader("vertex")]
VertexOutput main(VertexInput input) {
    VertexOutput output;
    float4 near_h = mul(
        material.u_inv_view_projection,
        float4(input.position, 0.0, 1.0));
    float4 far_h = mul(
        material.u_inv_view_projection,
        float4(input.position, 1.0, 1.0));
    float3 near_world = near_h.xyz / near_h.w;
    float3 far_world = far_h.xyz / far_h.w;
    output.dir = far_world - near_world;
    output.position = termin_to_native_clip(
        float4(input.position, 0.0, 1.0));
    return output;
}
@endstage

@stage fragment
struct FragmentInput {
    float4 screen_pos : SV_Position;
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
        float height = clamp(normalize(input.dir).z, -1.0, 1.0);
        float3 color;
        if (height >= 0.0) {
            float t = pow(height, max(material.u_skybox_top_exponent, 0.001));
            color = lerp(
                material.u_skybox_horizon_color.rgb,
                material.u_skybox_top_color.rgb,
                t);
        } else {
            float t = pow(-height, max(material.u_skybox_bottom_exponent, 0.001));
            color = lerp(
                material.u_skybox_horizon_color.rgb,
                material.u_skybox_bottom_color.rgb,
                t);
        }
        output.color = float4(color, 1.0);
    }
    return output;
}
@endstage

@endphase
