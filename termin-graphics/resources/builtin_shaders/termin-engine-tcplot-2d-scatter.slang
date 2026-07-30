import termin_prelude;

struct Tcplot2DScatterDraw {
  column_major float4x4 u_matrix;
  float4 u_color;
  float4 u_params; // viewport width, viewport height, diameter px, unused
};

[[TerminScope("draw")]]
ConstantBuffer<Tcplot2DScatterDraw> tcplot2d_scatter_draw;

struct VertexInput {
  float2 corner : POSITION;
  float2 data_pos : INSTANCEPOS;
};

struct VertexOutput {
  float4 position : SV_Position;
  float2 corner : TEXCOORD0;
};

[shader("vertex")] VertexOutput vs_main(VertexInput input) {
  VertexOutput output;
  float4 center =
      mul(tcplot2d_scatter_draw.u_matrix, float4(input.data_pos, 0.0, 1.0));
  float2 offset_ndc = input.corner * tcplot2d_scatter_draw.u_params.z /
                      max(tcplot2d_scatter_draw.u_params.xy, float2(1.0)) * 2.0;
  output.position = termin_to_native_clip(
      float4(center.xy + offset_ndc * center.w, center.z, center.w));
  output.corner = input.corner * 2.0;
  return output;
}

struct FragmentOutput {
  float4 color : SV_Target0;
};

[shader("fragment")] FragmentOutput fs_main(VertexOutput input) {
  if (dot(input.corner, input.corner) > 1.0)
    discard;
  FragmentOutput output;
  output.color = tcplot2d_scatter_draw.u_color;
  return output;
}
