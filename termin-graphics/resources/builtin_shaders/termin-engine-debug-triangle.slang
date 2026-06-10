// Debug triangle — solid-color wireframe triangles for editor gizmos.
// No resources — pure passthrough.

// --- vertex ---
struct VIn  { float3 pos : POSITION; float4 col : COLOR0; };
struct VOut { float4 pos : SV_Position;  float4 col : COLOR0; };
[shader("vertex")] VOut vs_main(VIn i) { VOut o; o.pos = float4(i.pos, 1); o.col = i.col; return o; }

// --- fragment ---
struct FIn  { float4 col : COLOR0; };
struct FOut { float4 color : SV_Target0; };
[shader("fragment")] FOut fs_main(FIn i) { FOut o; o.color = i.col; return o; }
