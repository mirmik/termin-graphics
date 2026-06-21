// Present blit — copies a texture to the output framebuffer.
//

import termin_prelude;

[[TerminScope("transient")]]
Sampler2D u_tex;

// --- vertex ---
struct VIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; };
struct VOut { float4 pos : SV_Position;  float2 uv : TEXCOORD0; };
[shader("vertex")] VOut vs_main(VIn i) { VOut o; o.uv = i.uv; o.pos = float4(i.pos, 0, 1); return o; }

// --- fragment ---
struct FIn  { float4 screen_pos : SV_Position; float2 uv : TEXCOORD0; };
struct FOut { float4 color : SV_Target0; };
[shader("fragment")] FOut fs_main(FIn i) { FOut o; o.color = u_tex.Sample(i.uv); return o; }
