import termin_prelude;

static const int TERMIN_MAX_BONES = 128;

struct TerminBoneBlock {
    column_major float4x4 u_bone_matrices[TERMIN_MAX_BONES];
    int u_bone_count;
};

// The deformation resource belongs to the transform provider. Static entry
// points do not reference it, so backend reflection omits it from static
// variants while every skinned profile shares this one declaration.
[[TerminScope("draw")]]
ConstantBuffer<TerminBoneBlock> bone_block;

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

struct TerminWorldPositionNormal {
    float3 position;
    float3 normal;
};

float3 termin_skinned_position(
    float3 position,
    float4 joints,
    float4 weights,
    TerminBoneBlock bones
) {
    if (bones.u_bone_count <= 0) {
        return position;
    }

    float4 skinned_position = float4(0.0, 0.0, 0.0, 0.0);
    float total_weight = 0.0;
    for (int i = 0; i < 4; ++i) {
        int bone_index = int(joints[i]);
        float weight = weights[i];
        if (weight > 0.0 && bone_index >= 0 && bone_index < bones.u_bone_count) {
            skinned_position += weight * mul(bones.u_bone_matrices[bone_index], float4(position, 1.0));
            total_weight += weight;
        }
    }

    return total_weight > 0.0 ? skinned_position.xyz : position;
}

float3 termin_skinned_normal(
    float3 normal,
    float4 joints,
    float4 weights,
    TerminBoneBlock bones
) {
    if (bones.u_bone_count <= 0) {
        return normal;
    }

    float3 skinned_normal = float3(0.0, 0.0, 0.0);
    float total_weight = 0.0;
    for (int i = 0; i < 4; ++i) {
        int bone_index = int(joints[i]);
        float weight = weights[i];
        if (weight > 0.0 && bone_index >= 0 && bone_index < bones.u_bone_count) {
            skinned_normal += weight * mul((float3x3)bones.u_bone_matrices[bone_index], normal);
            total_weight += weight;
        }
    }

    return total_weight > 0.0 ? skinned_normal : normal;
}

float3 termin_static_world_position(float3 position, float4x4 model) {
    return mul(model, float4(position, 1.0)).xyz;
}

float3 termin_skinned_world_position(
    float3 position,
    float4 joints,
    float4 weights,
    float4x4 model,
    TerminBoneBlock bones
) {
    float3 local_position =
        termin_skinned_position(position, joints, weights, bones);
    return termin_static_world_position(local_position, model);
}

TerminWorldPositionNormal termin_static_world_position_normal(
    float3 position,
    float3 normal,
    float4x4 model
) {
    TerminWorldPositionNormal output;
    output.position = termin_static_world_position(position, model);
    output.normal = normalize(mul((float3x3)model, normal));
    return output;
}

TerminWorldPositionNormal termin_skinned_world_position_normal(
    float3 position,
    float3 normal,
    float4 joints,
    float4 weights,
    float4x4 model,
    TerminBoneBlock bones
) {
    float3 local_position =
        termin_skinned_position(position, joints, weights, bones);
    float3 local_normal =
        termin_skinned_normal(normal, joints, weights, bones);
    return termin_static_world_position_normal(local_position, local_normal, model);
}

TerminWorldVertex termin_static_world_vertex(
    float3 position,
    float3 normal,
    float2 uv,
    float4 tangent,
    float4x4 model
) {
    TerminWorldVertex output;
    float4 world = mul(model, float4(position, 1.0));
    float3x3 normal_matrix = (float3x3)model;
    output.position = world.xyz;
    output.normal = normalize(mul(normal_matrix, normal));
    output.uv = uv;
    output.tangent = float4(0.0, 0.0, 0.0, 0.0);
    if (length(tangent.xyz) > 0.001) {
        float3 world_tangent = normalize(mul(normal_matrix, tangent.xyz));
        world_tangent = normalize(
            world_tangent - dot(world_tangent, output.normal) * output.normal);
        output.tangent = float4(world_tangent, tangent.w);
    }
    return output;
}

TerminWorldVertex termin_skinned_world_vertex(
    float3 position,
    float3 normal,
    float2 uv,
    float4 tangent,
    float4 joints,
    float4 weights,
    float4x4 model,
    TerminBoneBlock bones
) {
    float3 local_position =
        termin_skinned_position(position, joints, weights, bones);
    float3 local_normal =
        termin_skinned_normal(normal, joints, weights, bones);
    return termin_static_world_vertex(
        local_position, local_normal, uv, tangent, model);
}

TerminWorldVertex termin_static_vertex(
    TerminMeshVertexInput input,
    float4x4 model
) {
    return termin_static_world_vertex(
        input.position, input.normal, input.uv, input.tangent, model);
}

TerminWorldVertex termin_skinned_vertex(
    TerminSkinnedVertexInput input,
    float4x4 model,
    TerminBoneBlock bones
) {
    return termin_skinned_world_vertex(
        input.position,
        input.normal,
        input.uv,
        input.tangent,
        input.joints,
        input.weights,
        model,
        bones);
}
