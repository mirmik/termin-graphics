import termin_prelude;

struct Tcplot3DDraw {
    column_major float4x4 u_mvp;
    float4 u_params;          // z_min, z_max, surface_mode, colormap_id
    float4 u_surface_color;
    float4 u_axis_shading;    // axis_scale.xyz, shading_enabled
    float4 u_light_strength;  // light_dir.xyz, shading_strength
};

[[TerminScope("draw")]]
ConstantBuffer<Tcplot3DDraw> tcplot3d_draw;

struct VertexInput {
    float3 position : POSITION;
    float4 color : COLOR0;
    float4 surface_grid : SURFGRID;
    float4 surface_grid_color : SURFGRIDCOLOR;
    float4 surface_grid_opts : SURFGRIDOPTS;
};

struct VertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float z_norm : TEXCOORD0;
    float3 scaled_pos : TEXCOORD1;
    float4 surface_grid : TEXCOORD2;
    float4 surface_grid_color : TEXCOORD3;
    float4 surface_grid_opts : TEXCOORD4;
};

float3 jet(float t) {
    t = clamp(t, 0.0, 1.0);
    float r;
    float g;
    float b;
    if (t < 0.125) {
        r = 0.0; g = 0.0; b = 0.5 + t * 4.0;
    } else if (t < 0.375) {
        r = 0.0; g = (t - 0.125) * 4.0; b = 1.0;
    } else if (t < 0.625) {
        r = (t - 0.375) * 4.0; g = 1.0; b = 1.0 - (t - 0.375) * 4.0;
    } else if (t < 0.875) {
        r = 1.0; g = 1.0 - (t - 0.625) * 4.0; b = 0.0;
    } else {
        r = 1.0 - (t - 0.875) * 4.0; g = 0.0; b = 0.0;
    }
    return float3(r, g, b);
}

float3 viridis(float t) {
    t = clamp(t, 0.0, 1.0);
    float3 c0 = float3(0.267, 0.005, 0.329);
    float3 c1 = float3(0.283, 0.141, 0.458);
    float3 c2 = float3(0.254, 0.265, 0.530);
    float3 c3 = float3(0.207, 0.372, 0.553);
    float3 c4 = float3(0.164, 0.471, 0.558);
    float3 c5 = float3(0.128, 0.567, 0.551);
    float3 c6 = float3(0.135, 0.659, 0.518);
    float3 c7 = float3(0.267, 0.749, 0.441);
    float3 c8 = float3(0.478, 0.821, 0.318);
    float3 c9 = float3(0.741, 0.873, 0.150);
    float x = t * 9.0;
    int i = int(floor(x));
    float f = frac(x);
    if (i <= 0) return lerp(c0, c1, f);
    if (i == 1) return lerp(c1, c2, f);
    if (i == 2) return lerp(c2, c3, f);
    if (i == 3) return lerp(c3, c4, f);
    if (i == 4) return lerp(c4, c5, f);
    if (i == 5) return lerp(c5, c6, f);
    if (i == 6) return lerp(c6, c7, f);
    if (i == 7) return lerp(c7, c8, f);
    return lerp(c8, c9, f);
}

float3 plasma(float t) {
    t = clamp(t, 0.0, 1.0);
    float3 c0 = float3(0.050, 0.030, 0.528);
    float3 c1 = float3(0.362, 0.004, 0.649);
    float3 c2 = float3(0.610, 0.090, 0.620);
    float3 c3 = float3(0.798, 0.280, 0.470);
    float3 c4 = float3(0.928, 0.473, 0.326);
    float3 c5 = float3(0.994, 0.704, 0.184);
    float3 c6 = float3(0.940, 0.975, 0.131);
    float x = t * 6.0;
    int i = int(floor(x));
    float f = frac(x);
    if (i <= 0) return lerp(c0, c1, f);
    if (i == 1) return lerp(c1, c2, f);
    if (i == 2) return lerp(c2, c3, f);
    if (i == 3) return lerp(c3, c4, f);
    if (i == 4) return lerp(c4, c5, f);
    return lerp(c5, c6, f);
}

float3 cool_warm(float t) {
    t = clamp(t, 0.0, 1.0);
    float3 cool = float3(0.230, 0.299, 0.754);
    float3 mid = float3(0.865, 0.865, 0.865);
    float3 warm = float3(0.706, 0.016, 0.150);
    return (t < 0.5)
        ? lerp(cool, mid, t * 2.0)
        : lerp(mid, warm, (t - 0.5) * 2.0);
}

float3 map_surface_color(float t, float map_id) {
    int id = int(map_id + 0.5);
    if (id >= 100) {
        id -= 100;
        t = 1.0 - t;
    }

    float3 result = jet(t);
    if (id == 1) {
        result = viridis(t);
    } else if (id == 2) {
        result = plasma(t);
    } else if (id == 3) {
        result = float3(clamp(t, 0.0, 1.0));
    } else if (id == 4) {
        result = cool_warm(t);
    } else if (id == 5) {
        result = tcplot3d_draw.u_surface_color.rgb;
    }
    return result;
}

float grid_line_mask(float coord, float step, float max_coord, float width_px) {
    step = max(step, 1.0);
    float d_step = abs(coord - round(coord / step) * step);
    float d_edge = min(abs(coord), abs(coord - max_coord));
    float d = min(d_step, d_edge);
    float fw = max(fwidth(coord), 1e-6);
    float half_width = max(width_px, 0.1) * fw * 0.5;
    return 1.0 - smoothstep(half_width, half_width + fw, d);
}

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.position = termin_to_native_clip(mul(tcplot3d_draw.u_mvp, float4(input.position, 1.0)));
    output.color = input.color;
    output.scaled_pos = input.position * tcplot3d_draw.u_axis_shading.xyz;
    output.surface_grid = input.surface_grid;
    output.surface_grid_color = input.surface_grid_color;
    output.surface_grid_opts = input.surface_grid_opts;
    float z_range = tcplot3d_draw.u_params.y - tcplot3d_draw.u_params.x;
    output.z_norm = (z_range > 0.0)
        ? (input.position.z - tcplot3d_draw.u_params.x) / z_range
        : 0.5;
    return output;
}

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main(VertexOutput input) {
    FragmentOutput output;
    if (tcplot3d_draw.u_params.z != 0.0) {
        float3 rgb = map_surface_color(input.z_norm, tcplot3d_draw.u_params.w);
        if (tcplot3d_draw.u_axis_shading.w != 0.0) {
            float3 dx = ddx(input.scaled_pos);
            float3 dy = ddy(input.scaled_pos);
            float3 n = normalize(cross(dx, dy));
            float3 l = normalize(tcplot3d_draw.u_light_strength.xyz);
            float ndl = abs(dot(n, l));
            float strength = tcplot3d_draw.u_light_strength.w;
            float shade = 1.0 - strength + strength * ndl;
            shade = clamp(shade, 0.72, 1.08);
            rgb *= shade;
        }
        if (input.surface_grid_opts.x != 0.0) {
            float col_mask = grid_line_mask(
                input.surface_grid.x,
                input.surface_grid.z,
                input.surface_grid_opts.z,
                input.surface_grid_opts.y);
            float row_mask = grid_line_mask(
                input.surface_grid.y,
                input.surface_grid.w,
                input.surface_grid_opts.w,
                input.surface_grid_opts.y);
            float grid_mask = max(col_mask, row_mask);
            rgb = lerp(rgb, input.surface_grid_color.rgb,
                       clamp(input.surface_grid_color.a * grid_mask, 0.0, 1.0));
        }
        output.color = float4(rgb, tcplot3d_draw.u_surface_color.a);
    } else {
        output.color = input.color;
    }
    return output;
}
