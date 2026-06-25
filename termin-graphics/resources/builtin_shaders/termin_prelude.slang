// Termin Slang prelude.
//
// User attributes declared here are consumed by termin_shaderc through
// slangc reflection JSON. They describe Termin runtime metadata, not backend
// register placement.

[__AttributeUsage(_AttributeTargets.Var)]
public struct TerminScopeAttribute
{
    string value;
}
#ifndef TERMIN_NATIVE_CLIP_Y_UP
#define TERMIN_NATIVE_CLIP_Y_UP 0
#endif

public float4 termin_to_native_clip(float4 clip)
{
#if TERMIN_NATIVE_CLIP_Y_UP
    clip.y = -clip.y;
#endif
    return clip;
}

public float2 termin_clip_to_pixel(float4 clip, float2 viewport_size)
{
    float2 ndc = clip.xy / clip.w;
    return (ndc * 0.5 + 0.5) * viewport_size;
}

public float4 termin_pixel_to_clip(float2 pixel, float z, float w, float2 viewport_size)
{
    float2 safe_viewport = max(viewport_size, float2(1.0, 1.0));
    float2 ndc = pixel / safe_viewport * 2.0 - 1.0;
    return float4(ndc * w, z, w);
}
