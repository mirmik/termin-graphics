// Picking pass projection output.

import termin_prelude;

struct PerFrame {
    float4x4 view;
    float4x4 projection;
};

// The generated material-pipeline vertex entry point evaluates its world-space
// transform before calling termin_id_vertex_output(). Keep the model ABI in this
// imported vertex module so static and skinned ID variants can resolve it.
struct IdModelData {
    float4x4 model;
};

[[TerminScope("frame")]]
ConstantBuffer<PerFrame> per_frame;

[[TerminScope("draw")]]
ConstantBuffer<IdModelData> id_model;

struct VertexOutput {
    float4 position : SV_Position;
};

VertexOutput termin_id_vertex_output(float3 world_pos) {
    VertexOutput output;
    output.position = termin_to_native_clip(
        mul(per_frame.projection, mul(per_frame.view, float4(world_pos, 1.0))));
    return output;
}
