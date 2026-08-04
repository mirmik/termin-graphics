import termin_prelude;
import termin_lighting;

public static const int MAX_SHADOW_MAPS = 16;

public struct ShadowBlock {
    int u_shadow_map_count;
    // Explicit matrix rows keep the std140 footprint stable without nested
    // RowMajor/MatrixStride decorations, which older mobile Vulkan drivers
    // can miscompile for arrays of matrices.
    float4 u_light_space_matrix[MAX_SHADOW_MAPS * 4];
    int u_shadow_light_index[MAX_SHADOW_MAPS];
    int u_shadow_cascade_index[MAX_SHADOW_MAPS];
    float u_shadow_split_near[MAX_SHADOW_MAPS];
    float u_shadow_split_far[MAX_SHADOW_MAPS];
    // Row 1 of the camera view matrix. Termin uses +Y as view-space depth.
    float4 u_camera_view_depth;
    // xy = 1 / shadow-map dimensions. float4 keeps array stride explicit and
    // identical across std140, HLSL constant buffers and older Vulkan drivers.
    float4 u_shadow_texel_size[MAX_SHADOW_MAPS];
};

[[TerminScope("pass")]]
public ConstantBuffer<ShadowBlock> shadow_block;

[[TerminScope("pass")]]
public Sampler2DShadow shadow_maps[MAX_SHADOW_MAPS];

public static const int SHADOW_METHOD_HARD = 0;
public static const int SHADOW_METHOD_PCF = 1;
public static const int SHADOW_METHOD_POISSON = 2;

public static const int POISSON_SAMPLE_COUNT = 16;
public static const float2 POISSON_DISK[POISSON_SAMPLE_COUNT] = {
    float2(-0.94201624, -0.39906216),
    float2( 0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870),
    float2( 0.34495938,  0.29387760),
    float2(-0.91588581,  0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543,  0.27676845),
    float2( 0.97484398,  0.75648379),
    float2( 0.44323325, -0.97511554),
    float2( 0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023),
    float2( 0.79197514,  0.19090188),
    float2(-0.24188840,  0.99706507),
    float2(-0.81409955,  0.91437590),
    float2( 0.19984126,  0.78641367),
    float2( 0.14383161, -0.14100790),
};

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

public float sample_shadow_filtered(int sm, float2 uv, float depth) {
    int method = get_shadow_method();
    if (method == SHADOW_METHOD_HARD) {
        // The comparison sampler is linear, so a single lookup gives the
        // portable hardware 2x2 PCF normally expected from a hard shadow.
        return sample_shadow_map(sm, uv, depth);
    }

    float2 texel_size = shadow_block.u_shadow_texel_size[sm].xy;
    float softness = max(get_shadow_softness(), 0.0);
    if (method == SHADOW_METHOD_POISSON) {
        float visibility = 0.0;
        float radius = 2.5 * softness;
        for (int i = 0; i < POISSON_SAMPLE_COUNT; ++i) {
            visibility += sample_shadow_map(
                sm,
                uv + POISSON_DISK[i] * texel_size * radius,
                depth);
        }
        return visibility / float(POISSON_SAMPLE_COUNT);
    }

    // Unknown values deliberately use the default PCF method, matching the
    // scene setting's default and keeping corrupted data visibly filtered.
    float visibility = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            visibility += sample_shadow_map(
                sm,
                uv + float2(float(x), float(y)) * texel_size * softness,
                depth);
        }
    }
    return visibility / 25.0;
}

public float compute_shadow_candidate(int sm, int light_index, float3 world_pos) {
    if (shadow_block.u_shadow_map_count <= sm ||
        shadow_block.u_shadow_light_index[sm] != light_index) {
        return -1.0;
    }

    float4 world = float4(world_pos, 1.0);
    int row = sm * 4;
    float4 light_space_pos = float4(
        dot(shadow_block.u_light_space_matrix[row + 0], world),
        dot(shadow_block.u_light_space_matrix[row + 1], world),
        dot(shadow_block.u_light_space_matrix[row + 2], world),
        dot(shadow_block.u_light_space_matrix[row + 3], world));
    float3 proj_coords = light_space_pos.xyz / light_space_pos.w;
    proj_coords.xy = proj_coords.xy * 0.5 + 0.5;

    if (proj_coords.x < 0.0 || proj_coords.x > 1.0 ||
        proj_coords.y < 0.0 || proj_coords.y > 1.0 ||
        proj_coords.z < 0.0 || proj_coords.z > 1.0) {
        return -1.0;
    }

    return sample_shadow_filtered(
        sm,
        proj_coords.xy,
        proj_coords.z - shadow_bias_depth(sm));
}

public int find_shadow_map_for_cascade(int light_index, int cascade_index) {
    for (int sm = 0; sm < shadow_block.u_shadow_map_count; ++sm) {
        if (shadow_block.u_shadow_light_index[sm] == light_index &&
            shadow_block.u_shadow_cascade_index[sm] == cascade_index) {
            return sm;
        }
    }
    return -1;
}

public float compute_shadow_auto(int light_index, float3 world_pos) {
    int cascade_count = max(get_light_cascade_count(light_index), 1);
    if (cascade_count == 1) {
        int sm = find_shadow_map_for_cascade(light_index, 0);
        if (sm < 0) return 1.0;
        float visibility = compute_shadow_candidate(sm, light_index, world_pos);
        return visibility >= 0.0 ? visibility : 1.0;
    }

    float4 world = float4(world_pos, 1.0);
    float view_depth = dot(shadow_block.u_camera_view_depth, world);
    int cascade_sm = -1;
    for (int sm = 0; sm < shadow_block.u_shadow_map_count; ++sm) {
        if (shadow_block.u_shadow_light_index[sm] != light_index) continue;
        float split_near = shadow_block.u_shadow_split_near[sm];
        float split_far = shadow_block.u_shadow_split_far[sm];
        if (view_depth >= split_near && view_depth <= split_far) {
            cascade_sm = sm;
            break;
        }
    }

    if (cascade_sm < 0) return 1.0;
    float primary = compute_shadow_candidate(cascade_sm, light_index, world_pos);
    if (primary < 0.0) primary = 1.0;

    if (!get_light_cascade_blend(light_index)) return primary;
    int cascade_index = shadow_block.u_shadow_cascade_index[cascade_sm];
    if (cascade_index + 1 >= cascade_count) return primary;

    float split_far = shadow_block.u_shadow_split_far[cascade_sm];
    float split_near = shadow_block.u_shadow_split_near[cascade_sm];
    float blend_distance = min(
        max(get_light_blend_distance(light_index), 0.0),
        max(split_far - split_near, 0.0));
    if (blend_distance <= 0.0 || view_depth < split_far - blend_distance) {
        return primary;
    }

    int next_sm = find_shadow_map_for_cascade(light_index, cascade_index + 1);
    if (next_sm < 0) return primary;
    float secondary = compute_shadow_candidate(next_sm, light_index, world_pos);
    if (secondary < 0.0) return primary;
    float blend = saturate((view_depth - (split_far - blend_distance)) / blend_distance);
    return lerp(primary, secondary, blend);
}
