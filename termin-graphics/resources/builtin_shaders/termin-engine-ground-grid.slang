import termin_prelude;

struct GridParamsData {
    column_major float4x4 u_inv_vp;
    column_major float4x4 u_view;
    column_major float4x4 u_projection;
    float u_near;
    float u_far;
};

[[TerminScope("draw")]]
ConstantBuffer<GridParamsData> GridParams;

struct VertexInput {
    float2 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 near_point : TEXCOORD0;
    float3 far_point : TEXCOORD1;
};

float3 unproject(float2 xy, float z) {
    float4 p = mul(GridParams.u_inv_vp, float4(xy, z, 1.0));
    return p.xyz / p.w;
}

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.near_point = unproject(input.position, 0.0);
    output.far_point = unproject(input.position, 1.0);
    output.position = float4(input.position, 0.0, 1.0);
    return output;
}

struct FragmentInput {
    float4 screen_pos : SV_Position;
    float3 near_point : TEXCOORD0;
    float3 far_point : TEXCOORD1;
};

struct FragmentOutput {
    float4 color : SV_Target0;
    float depth : SV_Depth;
};

float4 grid(float3 pos, float scale, float4 color) {
    float2 coord = pos.xy / scale;
    float2 d = fwidth(coord);
    float2 grid_line = abs(fract(coord - 0.5) - 0.5) / d;
    float line = min(grid_line.x, grid_line.y);
    float alpha = 1.0 - min(line, 1.0);
    return float4(color.rgb, color.a * alpha);
}

float compute_depth(float3 pos) {
    float4 clip = mul(GridParams.u_projection, mul(GridParams.u_view, float4(pos, 1.0)));
    return (clip.z / clip.w) * 0.5 + 0.5;
}

float compute_fade(float3 pos) {
    float4 clip = mul(GridParams.u_projection, mul(GridParams.u_view, float4(pos, 1.0)));
    float ndc_depth = clip.z / clip.w;
    float linear_depth =
        (2.0 * GridParams.u_near * GridParams.u_far) /
        (GridParams.u_far + GridParams.u_near - ndc_depth * (GridParams.u_far - GridParams.u_near));
    return max(0.0, 1.0 - linear_depth / GridParams.u_far);
}

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    float3 ray = input.far_point - input.near_point;
    float t = -input.near_point.z / ray.z;
    if (t < 0.0) {
        discard;
    }

    float3 world_pos = input.near_point + t * ray;

    float4 small_grid = grid(world_pos, 1.0, float4(0.5, 0.5, 0.5, 0.3));
    float4 large_grid = grid(world_pos, 10.0, float4(0.5, 0.5, 0.5, 0.5));

    float2 dxy = fwidth(float2(world_pos.y, world_pos.x));
    float x_axis = 1.0 - min(abs(world_pos.y) / dxy.x, 1.0);
    float y_axis = 1.0 - min(abs(world_pos.x) / dxy.y, 1.0);

    float4 color = small_grid + large_grid * (1.0 - small_grid.a);
    color.rgb = lerp(color.rgb, float3(0.8, 0.2, 0.2), x_axis * 0.8);
    color.rgb = lerp(color.rgb, float3(0.2, 0.8, 0.2), y_axis * 0.8);
    color.a = max(color.a, max(x_axis * 0.8, y_axis * 0.8));

    float fade = compute_fade(world_pos);
    color.a *= fade;

    FragmentOutput output;
    output.depth = compute_depth(world_pos);
    output.color = color;
    return output;
}
