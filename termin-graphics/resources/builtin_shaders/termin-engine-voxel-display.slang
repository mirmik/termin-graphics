import termin_prelude;

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
    column_major float4x4 model;
};

[[TerminScope("draw")]]
ConstantBuffer<DrawData> draw_data;

struct VoxelDisplayParams {
    float4 u_color_below;
    float4 u_color_above;
    float4 u_slice_axis_fill_percent;
    float4 u_bounds_min;
    float4 u_bounds_max;
};

[[TerminScope("material")]]
ConstantBuffer<VoxelDisplayParams> material;

struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 color : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 world_pos : POSITION1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    float4 world = mul(draw_data.model, float4(input.position, 1.0));

    output.position = mul(per_frame.u_view_projection, world);
    output.world_pos = world.xyz;
    output.normal = mul((float3x3)draw_data.model, input.normal);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

struct FragmentInput {
    float4 screen_pos : SV_Position;
    float3 world_pos : POSITION1;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 color : COLOR0;
};

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    float3 bounds_min = material.u_bounds_min.xyz;
    float3 bounds_max = material.u_bounds_max.xyz;
    float3 slice_axis = material.u_slice_axis_fill_percent.xyz;
    float fill_percent = material.u_slice_axis_fill_percent.w;

    float3 world_bounds_min = mul(draw_data.model, float4(bounds_min, 1.0)).xyz;
    float3 world_bounds_max = mul(draw_data.model, float4(bounds_max, 1.0)).xyz;
    float3 world_slice_axis = normalize(mul((float3x3)draw_data.model, slice_axis));

    float axis_min = dot(world_bounds_min, world_slice_axis);
    float axis_max = dot(world_bounds_max, world_slice_axis);
    float axis_range = axis_max - axis_min;
    float pos_on_axis = dot(input.world_pos, world_slice_axis);

    float normalized_pos = 0.5;
    if (axis_range > 0.0001) {
        normalized_pos = (pos_on_axis - axis_min) / axis_range;
    }

    float blend_zone = 0.05;
    float cut_threshold = fill_percent + blend_zone;
    if (normalized_pos > cut_threshold) {
        discard;
    }

    float4 base_color;
    bool use_vertex_color = input.uv.x >= 1.5;
    if (use_vertex_color) {
        base_color = float4(input.color, 1.0);
    } else if (normalized_pos > fill_percent) {
        float t = (normalized_pos - fill_percent) / blend_zone;
        base_color = lerp(material.u_color_below, material.u_color_above, t);
    } else {
        base_color = material.u_color_below;
    }

    float3 normal = normalize(input.normal);
    float3 light_dir = normalize(float3(0.5, 0.8, 1.0));
    float ndotl = max(dot(normal, light_dir), 0.0);
    float3 ambient = float3(0.4);
    float3 diffuse = base_color.rgb * (ambient + ndotl * 0.6);

    FragmentOutput output;
    output.color = float4(diffuse, base_color.a);
    return output;
}
