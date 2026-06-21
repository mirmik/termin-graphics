import termin_prelude;
import termin_lighting;

public static const int MAX_SHADOW_MAPS = 16;

public struct ShadowBlock {
    int u_shadow_map_count;
    column_major float4x4 u_light_space_matrix[MAX_SHADOW_MAPS];
    int u_shadow_light_index[MAX_SHADOW_MAPS];
    int u_shadow_cascade_index[MAX_SHADOW_MAPS];
    float u_shadow_split_near[MAX_SHADOW_MAPS];
    float u_shadow_split_far[MAX_SHADOW_MAPS];
};

[[TerminScope("pass")]]
public ConstantBuffer<ShadowBlock> shadow_block;

[[TerminScope("pass")]]
public Sampler2DShadow shadow_maps[MAX_SHADOW_MAPS];

public float shadow_bias_depth(int sm) {
    float depth_range = max(
        shadow_block.u_shadow_split_far[sm] - shadow_block.u_shadow_split_near[sm],
        0.0001);
    return max(get_shadow_bias(), 0.0) / depth_range;
}

public float sample_shadow_map(int sm, float2 uv, float depth) {
    switch (sm) {
        case 0: return shadow_maps[0].SampleCmp(uv, depth);
        case 1: return shadow_maps[1].SampleCmp(uv, depth);
        case 2: return shadow_maps[2].SampleCmp(uv, depth);
        case 3: return shadow_maps[3].SampleCmp(uv, depth);
        case 4: return shadow_maps[4].SampleCmp(uv, depth);
        case 5: return shadow_maps[5].SampleCmp(uv, depth);
        case 6: return shadow_maps[6].SampleCmp(uv, depth);
        case 7: return shadow_maps[7].SampleCmp(uv, depth);
        case 8: return shadow_maps[8].SampleCmp(uv, depth);
        case 9: return shadow_maps[9].SampleCmp(uv, depth);
        case 10: return shadow_maps[10].SampleCmp(uv, depth);
        case 11: return shadow_maps[11].SampleCmp(uv, depth);
        case 12: return shadow_maps[12].SampleCmp(uv, depth);
        case 13: return shadow_maps[13].SampleCmp(uv, depth);
        case 14: return shadow_maps[14].SampleCmp(uv, depth);
        case 15: return shadow_maps[15].SampleCmp(uv, depth);
        default: return 1.0;
    }
}

public float compute_shadow_candidate(int sm, int light_index, float3 world_pos) {
    if (shadow_block.u_shadow_map_count <= sm ||
        shadow_block.u_shadow_light_index[sm] != light_index) {
        return -1.0;
    }

    float4 light_space_pos =
        mul(shadow_block.u_light_space_matrix[sm], float4(world_pos, 1.0));
    float3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    proj_coords.xy = proj_coords.xy * 0.5 + 0.5;

    if (proj_coords.x < 0.0 || proj_coords.x > 1.0 ||
        proj_coords.y < 0.0 || proj_coords.y > 1.0 ||
        proj_coords.z < 0.0 || proj_coords.z > 1.0) {
        return -1.0;
    }

    return sample_shadow_map(
        sm,
        proj_coords.xy,
        proj_coords.z - shadow_bias_depth(sm));
}

public float compute_shadow_auto(int light_index, float3 world_pos) {
    float shadow = compute_shadow_candidate(0, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(1, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(2, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(3, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(4, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(5, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(6, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(7, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(8, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(9, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(10, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(11, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(12, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(13, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(14, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    shadow = compute_shadow_candidate(15, light_index, world_pos);
    if (shadow >= 0.0) return shadow;
    return 1.0;
}
