// Immediate renderer — vertex-color triangles/lines for UI/debug.
//

import termin_prelude;

struct ImmPush { float4x4 mvp; };

[[TerminScope("draw")]]
ConstantBuffer<ImmPush> u_push;

// --- vertex ---
struct VIn  { float3 pos : POSITION; float4 col : COLOR0; };
struct VOut { float4 pos : SV_Position;  float4 col : COLOR0; };
[shader("vertex")] VOut vs_main(VIn i) { VOut o; o.pos = termin_to_native_clip(mul(u_push.mvp, float4(i.pos, 1))); o.col = i.col; return o; }

// --- fragment ---
struct FIn  { float4 col : COLOR0; };
struct FOut { float4 color : SV_Target0; };
[shader("fragment")] FOut fs_main(FIn i) { FOut o; o.color = i.col; return o; }
