# Pipeline Layout Architecture

Date: 2026-06-10
Updated: 2026-06-11

## Principle

**Shader source declares logical resources. Runtime binds them by name. Backend
set/binding numbers are generated layout metadata.**

Termin is migrating away from the old fixed shared Vulkan descriptor layout.
The current Vulkan backend already reflects descriptor bindings from SPIR-V
bytecode at `create_shader()` time and builds a per-pipeline
`VkDescriptorSetLayout` at `create_pipeline()` time.

That current implementation is intentionally transitional: it uses one Vulkan
descriptor set per pipeline. The target runtime model is scope-first
bind-by-name, with backend multiple descriptor sets introduced later as a
mechanical mapping from resource scopes.

See also:

- [Shader Resource Contracts](shader-resource-contracts.md)
- `docs/plans/2026-06-11-slang-scope-first-binding.md`
- `docs/plans/2026-06-09-slang-bind-by-name-runtime.md`

## Current Vulkan Data Flow

```text
GLSL / Slang source
  |
  v
compile (shaderc / slangc)
SPIR-V bytecode
  |
  +-- reflect_spirv_descriptor_bindings()
  |     binding number, VkDescriptorType, descriptor count
  |
  v
create_pipeline(vs, fs)
  |
  +-- merge VS + FS bindings
  +-- build/cache one VkDescriptorSetLayout
  |
  v
VkPipelineLayout
  + one descriptor set layout
  + 128-byte VkPushConstantRange
```

This removed the old universal layout table and default-texture filler behavior.
Pipelines only declare bindings that their shader pair actually uses.

## Target Runtime Model

Shader layout metadata should describe each resource as:

```text
name + kind + scope + backend placement
```

Scopes represent update frequency and ownership:

| Scope | Examples |
|---|---|
| `frame` | `per_frame` |
| `pass` | `lighting`, `shadow_block`, `shadow_maps` |
| `material` | `material`, material textures |
| `draw` | `draw_data`, large object constants |
| `transient` | fullscreen input textures, scratch pass resources |

Render pass code should move toward:

```cpp
ctx.bind_uniform("per_frame", per_frame_buffer);
ctx.bind_texture("albedo_texture", albedo);
ctx.bind_uniform("draw_data", draw_buffer);
```

The active shader layout resolves names to backend placement. Vulkan can later
map scopes to descriptor sets, while OpenGL flattens the same metadata to
binding numbers.

Explicit Slang resource scope should be authored through a Termin user
attribute supplied by an engine prelude:

```slang
import termin_prelude;

[[TerminScope("pass")]]
ConstantBuffer<LightingData> lighting;
```

The verified reflection path is `slangc -reflection-json` `userAttribs`.
Do not use `[[termin::scope("...")]]`: `slangc 2026.1-52-gc8ddf20bb` treats the
namespaced form as an unknown attribute and omits it from reflection JSON.

## Current Invariants

- No universal binding table should be reintroduced.
- Author-authored Slang sources should not contain `[[vk::...]]` or backend
  `register(...)` layout syntax.
- Vulkan supports only descriptor set `0` in the current reflected backend
  path. Non-zero `DescriptorSet` decorations are an error until scoped Vulkan
  sets are implemented deliberately.
- Missing layout metadata for migrated Slang artifact-required shaders should
  fail loudly.
- Numeric binding APIs remain as a legacy compatibility layer. New migrated
  passes should use bind-by-name.
- Duplicate reflected backend bindings with incompatible descriptor type/count
  must be diagnosed instead of silently merged.

## Transitional Numeric Paths

Some existing GLSL/material paths still bind numeric slots directly:

```cpp
ctx2->bind_uniform_buffer_ring(2, &pf, sizeof(pf));
ctx2->bind_uniform_buffer_ring(3, &shadow, sizeof(shadow));
ctx2->bind_sampled_texture_array_element(8, i, shadow_map);
```

Those paths are compatibility debt. They should be migrated by first adding
resource scope metadata, then resolving by logical name. Do not add new numeric
slot assignments for Slang code.

## Why Not Finish Single-Set First

Finishing the whole migration on one undifferentiated resource set would bake
in the wrong lifetime model. Frame, pass, material, and draw resources have
different update frequencies and should not be dirtied and rebound together.

The next architectural step is therefore scope-aware metadata and
`RenderContext2` state. Vulkan multiple descriptor sets can come after that,
when the frontend already knows which resources belong to each scope.
