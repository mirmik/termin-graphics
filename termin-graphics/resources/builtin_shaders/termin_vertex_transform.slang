import termin_prelude;

static const int TERMIN_MAX_BONES = 128;

struct TerminBoneBlock {
    float4x4 u_bone_matrices[TERMIN_MAX_BONES];
    int u_bone_count;
};

struct TerminMeshVertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
};

struct TerminSkinnedVertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT;
    float4 joints : BLENDINDICES0;
    float4 weights : BLENDWEIGHT0;
};

struct TerminWorldVertex {
    float3 position;
    float3 normal;
    float2 uv;
    float4 tangent;
};

TerminWorldVertex termin_static_vertex(
    TerminMeshVertexInput input,
    float4x4 model
) {
    TerminWorldVertex output;
    float4 world = mul(model, float4(input.position, 1.0));
    float3x3 normal_matrix = (float3x3)model;
    output.position = world.xyz;
    output.normal = normalize(mul(normal_matrix, input.normal));
    output.uv = input.uv;
    output.tangent = input.tangent;
    return output;
}

TerminWorldVertex termin_skinned_vertex(
    TerminSkinnedVertexInput input,
    float4x4 model,
    TerminBoneBlock bones
) {
    float3 local_position = input.position;
    float3 local_normal = input.normal;

    if (bones.u_bone_count > 0) {
        float4 skinned_position = float4(0.0, 0.0, 0.0, 0.0);
        float3 skinned_normal = float3(0.0, 0.0, 0.0);
        float total_weight = 0.0;

        for (int i = 0; i < 4; ++i) {
            int bone_index = int(input.joints[i]);
            float weight = input.weights[i];
            if (weight > 0.0 && bone_index >= 0 && bone_index < bones.u_bone_count) {
                float4x4 bone = bones.u_bone_matrices[bone_index];
                skinned_position += weight * mul(bone, float4(input.position, 1.0));
                skinned_normal += weight * mul((float3x3)bone, input.normal);
                total_weight += weight;
            }
        }

        if (total_weight > 0.0) {
            local_position = skinned_position.xyz;
            local_normal = skinned_normal;
        }
    }

    TerminMeshVertexInput static_input;
    static_input.position = local_position;
    static_input.normal = local_normal;
    static_input.uv = input.uv;
    static_input.tangent = input.tangent;
    return termin_static_vertex(static_input, model);
}
