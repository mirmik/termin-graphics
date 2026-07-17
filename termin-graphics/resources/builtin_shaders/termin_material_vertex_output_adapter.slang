// Standard material output owned by the color/material pass contract.

import termin_prelude;
import termin_vertex_transform;

struct PerFrame {
    column_major float4x4 u_view;
    column_major float4x4 u_projection;
    column_major float4x4 u_view_projection;
    column_major float4x4 u_inv_view;
    column_major float4x4 u_inv_proj;
    float4 u_camera_position;
    float2 u_resolution;
    float u_near;
    float u_far;
};

[[TerminScope("frame")]]
ConstantBuffer<PerFrame> per_frame;

// Mesh providers consume the model matrix from this pass declaration. The
// foliage provider does not reference it, so it is absent from foliage
// reflection and from the foliage transform contract.
struct DrawData {
    column_major float4x4 u_model;
};

[[TerminScope("draw")]]
ConstantBuffer<DrawData> draw_data;

struct VertexOutput {
    float4 position : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal_world : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 tangent_world : TEXCOORD3;
    float3 bitangent_world : TEXCOORD4;
    float tbn_valid : TEXCOORD5;
};

VertexOutput termin_material_vertex_output(TerminWorldVertex world) {
    VertexOutput output;
    float3 N = normalize(world.normal);

    output.world_pos = world.position;
    output.normal_world = N;
    output.uv = world.uv;
    output.tangent_world = float3(0.0, 0.0, 0.0);
    output.bitangent_world = float3(0.0, 0.0, 0.0);
    output.tbn_valid = 0.0;

    if (length(world.tangent.xyz) > 0.001) {
        float3 B = cross(N, world.tangent.xyz) * world.tangent.w;
        output.tangent_world = world.tangent.xyz;
        output.bitangent_world = B;
        output.tbn_valid = 1.0;
    }

    output.position = termin_to_native_clip(
        mul(per_frame.u_view_projection, float4(world.position, 1.0)));
    return output;
}
