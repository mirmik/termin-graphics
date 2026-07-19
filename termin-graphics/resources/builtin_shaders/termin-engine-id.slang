// ID pass — entity picking shader. Single source for vertex + fragment.
//

import termin_prelude;

struct PerFrame {
    float4x4 view;
    float4x4 projection;
};

struct IdModelData {
    float4x4 model;
};

struct IdPickData {
    float4 pick_color;
};

[[TerminScope("frame")]]
ConstantBuffer<PerFrame> per_frame;

[[TerminScope("draw")]]
ConstantBuffer<IdModelData> id_model;

[[TerminScope("draw")]]
ConstantBuffer<IdPickData> id_pick;

// --- vertex stage ---

struct VertexInput {
    float3 position : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.position = termin_to_native_clip(mul(per_frame.projection,
                     mul(per_frame.view,
                     mul(id_model.model, float4(input.position, 1.0)))));
    return output;
}

// --- fragment stage ---

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput fs_main() {
    FragmentOutput output;
    output.color = float4(id_pick.pick_color.rgb, 1.0);
    return output;
}
