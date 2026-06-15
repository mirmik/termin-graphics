# Shader Resource Contracts

Date: 2026-06-15

## Principle

Termin shader resources are a semantic contract first and a backend layout
second.

```text
Shader source owns semantic resource declarations.
Pass and termin_graphics code own semantic resource production and binding.
termin_shaderc owns backend placement.
Runtime resolves names through the active shader layout.
Backends consume resolved placement.
```

Normal shader authors and render-pass authors must not have to know Vulkan
descriptor bindings, OpenGL binding points, or D3D register indices. Those
details belong to generated artifacts and layout sidecars.

## Ownership

| Layer | Owns | Must not own |
|---|---|---|
| Slang shader source | Resource names, kinds, scopes, entry points, stage IO semantics | Backend `set`/`binding`, `register(...)`, `[[vk::...]]` placement |
| Pass / renderer code | Which logical resources are produced and bound for a draw/pass | Numeric backend slots |
| `termin_shaderc` | Backend placement allocation, validation, artifact sidecars | Runtime resource values |
| Runtime context | Name/scope/kind resolution against the active shader layout | Guessing missing numeric fallbacks |
| Backend | Applying already resolved placement to API objects | Reinterpreting Termin semantic contracts |

The public model is:

```text
source resource declaration
  -> termin_shaderc reflection and placement policy
  -> artifact + layout sidecar
  -> runtime bind-by-name
  -> backend descriptor/register binding
```

## Resource Scopes

Every migrated shader resource should have a logical scope. Scope describes
ownership and update frequency, not the final backend slot.

| Scope | Owner | Examples |
|---|---|---|
| `frame` | Frame setup | `per_frame` camera/frame constants |
| `pass` | Render pass | `lighting`, `shadow_block`, `shadow_maps`, pass params |
| `material` | Material system | `material`, material textures |
| `draw` | Draw submission | `draw_data`, `depth_draw`, `normal_draw`, `bone_block`, instance data |
| `transient` | Local pass graph edge | fullscreen inputs, scratch textures, temporary buffers |

Current Vulkan/OpenGL implementations may still flatten scopes into one
backend resource set. That is an implementation detail. Source, sidecars, and
runtime APIs should already behave as if scopes are distinct.

## Shader Authoring Contract

Slang source declares logical resources with explicit Termin scope metadata:

```slang
import termin_prelude;

struct MaterialParams {
    float4 base_color;
    float roughness;
};

[[TerminScope("material")]]
ConstantBuffer<MaterialParams> material;

[[TerminScope("material")]]
Sampler2D albedo_texture;
```

Shader source should not author backend placement:

```slang
// Do not add this to migrated Slang shaders.
[[vk::binding(4, 0)]]
Texture2D albedo_texture;

// Do not add this to migrated Slang shaders.
ConstantBuffer<MaterialParams> material : register(b1);
```

Stage inputs and outputs remain shader-owned and should use Slang/HLSL
semantics such as `POSITION`, `NORMAL`, `TEXCOORD0`, `SV_Position`, and
`SV_Target0`.

New production Slang resources should use explicit scope attributes. Name-based
scope inference is migration compatibility only and should shrink over time.

## Pass Authoring Contract

Pass and renderer code binds logical resources. It should not know backend
placement:

```cpp
ctx.bind_uniform_data("per_frame", &per_frame, sizeof(per_frame));
ctx.bind_uniform_data("lighting", &lighting, sizeof(lighting));
ctx.bind_texture("shadow_maps", shadow_atlas, shadow_sampler);
ctx.bind_uniform_data("draw_data", &draw_data, sizeof(draw_data));
```

For common engine resources, prefer typed helpers that centralize canonical
names and struct validation:

```cpp
bind_frame_camera(ctx, camera);
bind_lighting(ctx, lighting);
bind_shadow_resources(ctx, shadows);
bind_material_resources(ctx, material);
bind_draw_transform(ctx, transform);
```

Those helpers may still call bind-by-name internally. Their purpose is to keep
canonical resource names and struct packing contracts out of unrelated pass
logic.

## Compiler Contract

`termin_shaderc` is the owner of backend placement for migrated production
artifacts. It should:

- read Slang reflection and Termin scope metadata;
- reject invalid or missing scope metadata for artifact-required migrated
  resources;
- assign backend placement according to Termin policy;
- patch/emit artifacts so backend bytecode and sidecar metadata agree;
- write layout sidecars containing at least `name`, `kind`, `scope`, stage
  mask, size/fields for constant buffers, and backend placement;
- reject duplicate or incompatible backend placement conflicts;
- version layout sidecar semantics so ABI changes invalidate stale artifacts;
- produce diagnostics in resource-contract language, not only backend API
  language.

For example, prefer an error like:

```text
resource 'shadow_maps' is pass scope but overlaps material texture range
```

over:

```text
binding conflict at binding 8
```

Advanced shader-owned placement, if ever allowed, must be explicit opt-in and
validated against Termin reserved ranges. It is not the default authoring
model.

## Runtime Contract

Runtime binding is strict bind-by-name against the active shader layout.

The runtime must log and reject or skip invalid bindings when:

- no active shader layout is available for a migrated path;
- the shader layout does not declare the requested name;
- the declared kind does not match the bind call;
- uploaded uniform data exceeds the reflected constant-buffer size;
- backend placement conflicts with the active pipeline layout.

The runtime must not recover by guessing historical fixed slots. Numeric APIs
may remain as legacy compatibility for unmigrated GLSL paths, but new migrated
Slang paths should not add numeric slot usage.

## Backend Contract

Backends consume layout metadata after runtime resolution.

- Vulkan may map scopes to descriptor sets later; current single-set flattening
  is transitional.
- OpenGL may flatten the same metadata to binding points.
- D3D11 needs register-class-aware placement, such as `b#`, `t#`, `s#`, and
  `u#`; `kind` is therefore part of the backend placement key.

Backend code should not infer Termin resource meaning from fixed slots. It
should receive explicit resolved placement from shader layouts and resource set
descriptions.

## Adding A Resource

When adding a new migrated shader resource:

1. Pick the semantic owner and scope.
2. Pick a stable logical resource name.
3. Declare the resource in Slang with explicit Termin scope metadata.
4. Bind it from pass/material/draw code by logical name or typed helper.
5. Let `termin_shaderc` assign backend placement and write the sidecar.
6. Add validation or tests for missing name, kind mismatch, and relevant layout
   conflicts.
7. Update the relevant struct-layout documentation when the resource is a
   constant buffer.

Do not add a new global numeric binding assignment as part of this flow.

## Related Documents

- [Pipeline Layout Architecture](pipeline-layout.md) describes the current
  backend pipeline-layout data flow.
- [GPU pipeline layout](../../../docs/gpu-pipeline-layout.md) records CPU-side
  uniform layouts and vertex input details.
- [Slang scope-first binding migration](../../../docs/plans/2026-06-11-slang-scope-first-binding.md)
  is the migration plan that led to this contract.
- [Material Pipeline Slang migration](../../../docs/plans/2026-06-12-material-pipeline-slang-plan.md)
  describes material/pass/vertex-transform convergence.
