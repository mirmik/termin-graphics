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

struct DrawData {
    column_major float4x4 u_model;
};

[[TerminScope("draw")]]
ConstantBuffer<DrawData> draw_data;

[[TerminScope("draw")]]
ConstantBuffer<TerminBoneBlock> bone_block;

struct VertexOutput {
    float4 position : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal_world : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float3 tangent_world : TEXCOORD3;
    float3 bitangent_world : TEXCOORD4;
    float tbn_valid : TEXCOORD5;
};

[shader("vertex")]
VertexOutput vs_main(TerminSkinnedVertexInput input) {
    VertexOutput output;
    TerminWorldVertex world = termin_skinned_vertex(input, draw_data.u_model, bone_block);
    float3 N = normalize(world.normal);

    output.world_pos = world.position;
    output.normal_world = N;
    output.uv = world.uv;
    output.tangent_world = float3(0.0, 0.0, 0.0);
    output.bitangent_world = float3(0.0, 0.0, 0.0);
    output.tbn_valid = 0.0;

    float tangent_len = length(world.tangent.xyz);
    if (tangent_len > 0.001) {
        float3x3 normal_matrix = (float3x3)draw_data.u_model;
        float3 T = normalize(mul(normal_matrix, world.tangent.xyz));
        T = normalize(T - dot(T, N) * N);
        float3 B = cross(N, T) * world.tangent.w;
        output.tangent_world = T;
        output.bitangent_world = B;
        output.tbn_valid = 1.0;
    }

    output.position = mul(per_frame.u_view_projection, float4(world.position, 1.0));
    return output;
}
