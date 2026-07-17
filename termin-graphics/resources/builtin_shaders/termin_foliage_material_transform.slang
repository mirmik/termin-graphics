// Instanced foliage placement as a reusable material transform provider.

import termin_prelude;
import termin_vertex_transform;

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

TerminWorldVertex termin_foliage_material_world_vertex(
    float3 position,
    float3 normal,
    float2 uv,
    uint instance_id
) {
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
        yaw_right * position.x + yaw_forward * position.y + up * position.z;

    float3 source_normal = length(normal) > 0.0001
        ? normalize(normal)
        : float3(0.0, 0.0, 1.0);
    float3 local_normal = normalize(
        yaw_right * source_normal.x +
        yaw_forward * source_normal.y +
        up * source_normal.z);

    TerminWorldVertex output;
    output.position =
        mul(foliage_draw.u_position_model, float4(instance.position, 1.0)).xyz +
        mul(foliage_draw.u_vector_model, float4(local_offset, 0.0)).xyz;
    output.normal = normalize(
        mul(foliage_draw.u_vector_model, float4(local_normal, 0.0)).xyz);
    output.uv = uv;
    output.tangent = float4(0.0, 0.0, 0.0, 0.0);
    return output;
}
