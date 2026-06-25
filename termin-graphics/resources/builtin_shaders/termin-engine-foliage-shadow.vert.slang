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

struct FoliagePushData {
    column_major float4x4 u_position_model;
    column_major float4x4 u_vector_model;
};

[[TerminScope("draw")]]
ConstantBuffer<FoliagePushData> foliage_draw;

struct FoliageInstance {
    float3 position;
    float yaw;
    float3 normal;
    float seed;
};

[[TerminScope("draw")]]
StructuredBuffer<FoliageInstance> foliage_instances;

struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input, uint instance_id : SV_InstanceID) {
    VertexOutput output;
    FoliageInstance instance = foliage_instances[instance_id];

    float3 up = normalize(instance.normal);
    float3 helper = abs(up.z) > 0.85
        ? float3(0.0, 1.0, 0.0)
        : float3(0.0, 0.0, 1.0);
    float3 right = normalize(cross(helper, up));
    float3 forward = cross(up, right);

    float c = cos(instance.yaw);
    float s = sin(instance.yaw);
    float3 yaw_right = right * c + forward * s;
    float3 yaw_forward = -right * s + forward * c;
    float3 local_offset =
        yaw_right * input.position.x +
        yaw_forward * input.position.y +
        up * input.position.z;

    float3 world_pos =
        mul(foliage_draw.u_position_model, float4(instance.position, 1.0)).xyz +
        mul(foliage_draw.u_vector_model, float4(local_offset, 0.0)).xyz;
    output.position = termin_to_native_clip(mul(per_frame.u_view_projection, float4(world_pos, 1.0)));
    return output;
}
