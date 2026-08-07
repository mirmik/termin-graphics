// World-tube expansion owned by the LineRenderer vertex-transform provider.
// This module deliberately produces world-space data only. Projection belongs
// to the pass-owned VertexOutputAdapter (including its multiview SV_ViewID ABI).

import termin_vertex_transform;

float3 termin_tube_safe_normalize(float3 value, float3 fallback) {
    float value_length = length(value);
    return value_length >= 1.0e-6 ? value / value_length : fallback;
}

float3 termin_tube_axis0(float3 direction) {
    // Pick the least parallel cardinal axis. Unlike a camera-facing billboard,
    // this basis is identical for every view and therefore multiview-safe.
    float3 reference = abs(direction.y) < 0.9
        ? float3(0.0, 1.0, 0.0)
        : float3(1.0, 0.0, 0.0);
    return termin_tube_safe_normalize(
        cross(reference, direction), float3(0.0, 0.0, 1.0));
}

TerminWorldVertex termin_world_tube_vertex(
    float3 corner,
    float3 p0,
    float width,
    float3 p1
) {
    float3 direction = termin_tube_safe_normalize(
        p1 - p0, float3(1.0, 0.0, 0.0));
    float3 axis0 = termin_tube_axis0(direction);
    float3 axis1 = termin_tube_safe_normalize(
        cross(direction, axis0), float3(0.0, 1.0, 0.0));

    // Body corners use endpoint 0/1. Cap templates use 2 for the centre and 3
    // for the rim, allowing body and cap draws to share one planned shader.
    bool is_cap = corner.x >= 2.0;
    bool is_cap_center = corner.x < 2.5;
    float3 radial = axis0 * corner.y + axis1 * corner.z;

    TerminWorldVertex output;
    if (is_cap) {
        output.position = p0 + (is_cap_center ? float3(0.0) : radial * width * 0.5);
        output.normal = -direction;
        output.uv = is_cap_center
            ? float2(0.5, 0.5)
            : float2(corner.y * 0.5 + 0.5, corner.z * 0.5 + 0.5);
        output.tangent = float4(axis0, 1.0);
        return output;
    }

    output.position = lerp(p0, p1, corner.x) + radial * width * 0.5;
    output.normal = termin_tube_safe_normalize(radial, axis0);
    output.uv = float2(
        corner.x,
        atan2(corner.z, corner.y) / 6.28318530718 + 0.5);
    output.tangent = float4(direction, 1.0);
    return output;
}
