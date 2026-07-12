// Static transform provider for modular shadow vertex stages.
// The shadow adapter owns projection resources.  The provider invocation owns
// shadow_draw because the model matrix participates in the transform call.

float3 termin_shadow_static_world_position(
    float3 position,
    float4x4 model
) {
    return mul(model, float4(position, 1.0)).xyz;
}
