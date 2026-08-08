// Portable image-based-lighting vocabulary shared by material consumers.
// Directional maps use octahedral encoding in ordinary 2D textures so the
// shader ABI remains usable on backends without cubemap support.

import termin_prelude;

[[TerminScope("pass")]]
public Sampler2D ibl_diffuse_irradiance;

[[TerminScope("pass")]]
public Sampler2D ibl_prefiltered_specular;

[[TerminScope("pass")]]
public Sampler2D ibl_brdf_lut;

float termin_ibl_sign_not_zero(float value) {
    return value < 0.0 ? -1.0 : 1.0;
}

public float2 termin_ibl_octahedral_uv(float3 direction) {
    direction /= abs(direction.x) + abs(direction.y) + abs(direction.z);
    float2 uv = direction.xy;
    if (direction.z < 0.0) {
        uv = float2(
            (1.0 - abs(uv.y)) * termin_ibl_sign_not_zero(uv.x),
            (1.0 - abs(uv.x)) * termin_ibl_sign_not_zero(uv.y));
    }
    return uv * 0.5 + 0.5;
}

public float3 sample_ibl_diffuse_irradiance(float3 direction) {
    return ibl_diffuse_irradiance.Sample(termin_ibl_octahedral_uv(direction)).rgb;
}

public float3 sample_ibl_prefiltered_specular(float3 direction, float lod) {
    return ibl_prefiltered_specular.SampleLevel(termin_ibl_octahedral_uv(direction), lod).rgb;
}

public float2 sample_ibl_brdf(float normal_dot_view, float roughness) {
    return ibl_brdf_lut.Sample(float2(normal_dot_view, roughness)).rg;
}
