// Static transform provider for modular shadow vertex stages.
// The shadow adapter owns projection and draw-model resources; this provider
// only supplies the local-position to world-position transform operation.

float3 termin_shadow_static_world_position(
    float3 position,
    float4x4 model
) {
    return mul(model, float4(position, 1.0)).xyz;
}
