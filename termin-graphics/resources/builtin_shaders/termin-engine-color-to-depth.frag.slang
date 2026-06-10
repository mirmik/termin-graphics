// Color-to-depth pass — reads a color/R32F texture, writes depth.
//

Sampler2D u_color_tex;

struct FragmentInput {
    float2 uv : TEXCOORD0;
};

struct FragmentOutput {
    float depth : SV_Depth;
};

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input) {
    float d = u_color_tex.Sample(input.uv).r;

    FragmentOutput output;
    output.depth = clamp(d, 0.0, 1.0);
    return output;
}
