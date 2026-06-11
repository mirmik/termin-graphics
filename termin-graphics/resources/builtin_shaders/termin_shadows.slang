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

public float compute_shadow_auto(int light_index, float3 world_pos) {
    for (int sm = 0; sm < shadow_block.u_shadow_map_count; ++sm) {
        if (shadow_block.u_shadow_light_index[sm] != light_index) {
            continue;
        }

        float4 light_space_pos =
            mul(shadow_block.u_light_space_matrix[sm], float4(world_pos, 1.0));
        float3 proj_coords = light_space_pos.xyz / light_space_pos.w;
        proj_coords.xy = proj_coords.xy * 0.5 + 0.5;

        if (proj_coords.x < 0.0 || proj_coords.x > 1.0 ||
            proj_coords.y < 0.0 || proj_coords.y > 1.0 ||
            proj_coords.z < 0.0 || proj_coords.z > 1.0) {
            continue;
        }

        return shadow_maps[sm].SampleCmp(
            proj_coords.xy,
            proj_coords.z - shadow_bias_depth(sm));
    }

    return 1.0;
}
