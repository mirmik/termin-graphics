// Skinned transform provider for modular shadow vertex stages.  bone_block
// intentionally belongs here rather than to the shadow output adapter.

import termin_prelude;
import termin_vertex_transform;

[[TerminScope("draw")]]
ConstantBuffer<TerminBoneBlock> bone_block;

float3 termin_shadow_skinned_world_position(
    float3 position,
    float4 joints,
    float4 weights,
    float4x4 model,
    TerminBoneBlock bones
) {
    float3 local_position =
        termin_skinned_position(position, joints, weights, bones);
    return mul(model, float4(local_position, 1.0)).xyz;
}
