# Pipeline Layout Architecture

Date: 2026-06-10

## Principle

**The shader defines its descriptor set layout. The pipeline builds to match.**

No universal binding table. No hardcoded slot assignments. No default-texture
fillers. The Vulkan backend reflects descriptor bindings from SPIR-V bytecode
at `create_shader()` time and builds a per-pipeline `VkDescriptorSetLayout`
at `create_pipeline()` time.

This is the same architecture Godot 4 uses.

## Data flow

```
GLSL / Slang source
  │
  ▼ compile (shaderc / slangc)
SPIR-V bytecode
  │
  ├─ reflect_spirv_descriptor_bindings()  →  VkShaderResource::descriptor_bindings
  │    (binding number, VkDescriptorType, count per resource)
  │
  ▼ create_pipeline(vs, fs)
Merge VS + FS bindings → hash → get_or_create_descriptor_set_layout()
  │
  ▼
VkDescriptorSetLayout   (cached by FNV-1a hash of (binding, type, count))
  │
  ▼
VkPipelineLayout = get_or_create_pipeline_layout(dsl)
  + 128-byte VkPushConstantRange for all graphics stages
```

## What changed from the shared-layout era

| Before | After |
|---|---|
| `create_shared_layouts()` — 110-line fixed table | Deleted |
| 25 binding slots in every layout | Only the bindings the shader declares |
| `create_resource_set()` filled 16 default texture slots | No fillers — binds exactly what caller provides |
| Slang: `apply_slang_vulkan_shared_layout_policy()` forced all resources into slots 4–23 | Policy is removed; shader bindings pass through to SPIR-V |
| `DYNAMIC_UBO_BINDINGS = {0,1,2,3,16,24}` hardcoded | Dynamic UBO detected from ring-buffer handle at write time |
| `kMaxDescriptorSets = 2`, `set1_layout`, `set_count` | All deleted |

## Invariants

- **One descriptor set per pipeline.** No `set` index in the API (the parameter
  still exists with default 0 for backward compat, but it is unused).

- **Layout is derived from SPIR-V, not from policy.** If a shader declares
  `layout(binding = 7)`, that's where it goes. No remapping.

- **Layouts are cached.** Two pipelines with the same binding signature share
  one `VkDescriptorSetLayout` (FNV-1a hash). Pipeline layouts are cached by
  the layout handle.

- **No default-texture filling.** If a binding is not provided by the pass,
  Vulkan validation will report it. This catches binding mismatches that the
  old shared layout silently hid with a 1×1 white texture.

- **Shader handles are the cache key.** `PipelineCacheKey` already includes
  `vertex_shader` and `fragment_shader` — same shaders = same bindings, so
  no separate layout hash is needed.

## Render pass impact

None. Passes continue to bind resources at numeric slots. The shader declares
matching slots. Example (`color_pass.cpp`):

```cpp
ctx2->bind_uniform_buffer_ring(0, &pf, sizeof(pf));    // per_frame
ctx2->bind_uniform_buffer_ring(3, &sb, sizeof(sb));    // shadow_ubo
ctx2->bind_sampled_texture_array_element(8, i, tex);   // shadow map[i]
ctx2->bind_uniform_buffer(0, lighting_ubo);             // lighting
apply_material_phase_ubo(phase, shader, ...);           // material ubo + textures
ctx2->set_push_constants(&model, sizeof(model));        // u_model
```

The layout is built from the VS+FS pair bound by `ctx2->bind_shader(vs, fs)`.
`RenderContext2::flush_resource_set()` passes the pipeline's layout to
`IRenderDevice::create_resource_set()` through `ResourceSetDesc::descriptor_set_layout`.

## Future: symbolic binding

The numeric-slot pass code is the current state. The next step is a
symbolic bind-by-name API (`bind_uniform("per_frame", buf)`) that resolves
slots from shader reflection metadata. This is tracked separately in
`docs/plans/2026-06-09-slang-bind-by-name-runtime.md`.
