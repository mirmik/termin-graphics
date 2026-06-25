import termin_prelude;

struct Tcplot2DLineDraw {
    column_major float4x4 u_matrix;
    float4 u_color;
};

[[TerminScope("draw")]]
ConstantBuffer<Tcplot2DLineDraw> tcplot2d_line_draw;

struct VertexInput {
    float2 data_pos : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.position = mul(tcplot2d_line_draw.u_matrix, float4(input.data_pos, 0.0, 1.0));
    return output;
}

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main() {
    FragmentOutput output;
    output.color = tcplot2d_line_draw.u_color;
    return output;
}
