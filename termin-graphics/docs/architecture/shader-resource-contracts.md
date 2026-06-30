# Shader Resource Contracts

Date: 2026-06-15
Updated: 2026-06-30

## Principle

Termin shader resources are a semantic contract first and a backend layout
second.

```text
Shader source owns semantic resource declarations.
Pass and termin_graphics code own semantic resource production and binding.
termin_shaderc reflects declarations and emits compiler-side metadata.
Runtime resolves names through the active shader contract/layout.
Backend binding planners own concrete backend placement.
Backends consume resolved binding plans.
```

Normal shader authors and render-pass authors must not have to know Vulkan
descriptor bindings, OpenGL binding points, or D3D register indices. Those
details belong to generated artifacts and layout sidecars.

## Ownership

| Layer | Owns | Must not own |
|---|---|---|
| Slang shader source | Resource names, kinds, scopes, entry points, stage IO semantics | Backend `set`/`binding`, `register(...)`, `[[vk::...]]` placement |
| Pass / renderer code | Which logical resources are produced and bound for a draw/pass | Numeric backend slots, ad hoc fallback search through historical names |
| `termin_shaderc` | Reflection, artifact patching required by the target compiler, layout sidecars | Runtime resource values, inventing semantic ownership for arbitrary names |
| Runtime context | Name/scope/kind resolution against the active shader contract/layout | Guessing missing numeric fallbacks |
| Backend binding planner | Concrete backend placement and conflict validation | Semantic ownership policy |
| Backend | Applying already resolved binding plans to API objects | Reinterpreting Termin semantic contracts |

The public model is:

```text
source resource declaration
  -> termin_shaderc reflection and compiler metadata
  -> shader contract + layout metadata
  -> backend binding plan
  -> runtime bind-by-name
  -> backend descriptor/register/binding-point application
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
| `unscoped` | Unresolved compiler/import state | resource was reflected without Termin scope metadata |

Current Vulkan/OpenGL implementations may still flatten scopes into one
backend resource set. That is an implementation detail. Source, sidecars, and
runtime APIs should already behave as if scopes are distinct.

`unscoped` is not semantic ownership. It exists so missing metadata is not
silently treated as `transient` or any other real owner. Production artifact
pipelines should resolve it before runtime binding, either by adding explicit
`[[TerminScope(... )]]` metadata in source or by invoking `termin_shaderc` with
an appropriate default scope.

## Termin Shader ABI

Termin does not try to eliminate all well-known shader resource names. Render
engines normally have a shader ABI: a small set of standard resources whose
names, data layouts, scopes, and binding responsibilities are part of the
engine/material/pass contract. The migration target is therefore not "no magic
names"; it is "no ad hoc name guessing".

A well-known resource name is allowed when it is documented as ABI and validated
as ABI. It is not allowed to grow hidden fallback behavior in unrelated code.

Current fixed ABI resources:

| Resource name | Scope | Kind | Producer / binder | Payload / role |
|---|---|---|---|---|
| `per_frame` | `frame` | constant buffer | frame/pass setup helpers | camera, projection, viewport, and frame constants |
| `draw_data` | `draw` | constant buffer | draw submission / vertex-transform path | per-draw transform data |
| `material` | `material` | constant buffer | material system | generated material parameter block |
| `bone_block` | `draw` | constant buffer | skinned vertex-transform path | skinning matrices / bone data |
| `lighting` | `pass` | constant buffer | lighting/color pass | scene lights and ambient lighting data |
| `shadow_block` | `pass` | constant buffer | lighting/color pass | shadow matrix and cascade metadata |
| `shadow_maps` | `pass` | texture array | lighting/color pass | shadow map textures sampled by material shaders |

These names are stable shader ABI, not backend placement policy. Material
texture property names are different: `albedo_texture`, `normal_texture`, and
other material-declared textures are shader/material contract resources with
`material` scope, not fixed global ABI vocabulary entries.

A shader that uses Termin's standard lighting include should declare
`lighting`, `shadow_block`, and `shadow_maps` with `pass` scope. A shader that
does not use lighting need not declare those resources, and the pass should
skip those binds without guessing replacement names.

Adding a new well-known ABI resource requires updating this table, documenting
the payload layout, adding validation/tests, and deciding whether it belongs to
the core ABI or to a narrower pass/material ABI. Do not introduce a new
well-known name as a local string literal hidden inside one pass, compiler path,
or runtime helper.

### Alias Policy

Canonical names must have one spelling in new sources and generated contracts.
Historical aliases are migration debt, not peer ABI. Aliases may exist only in a
central compatibility path with diagnostics or tests that describe why they are
still accepted.

Known alias debt:

| Canonical | Legacy / accepted alias | Target |
|---|---|---|
| `draw_data` | `draw` | keep only as temporary compatibility for old C macro/runtime paths, then remove |
| `lighting` | `lighting_ubo`, `LightingBlock` | migrate shader/resource apply paths to the canonical ABI name |
| `shadow_block` | `ShadowBlock` | migrate shader/resource apply paths to the canonical ABI name |
| `shadow_maps` | `u_shadow_map` | migrate shader/resource apply paths to the canonical ABI name |

Alias lookup must not live as private arrays in pass code. If an alias is still
needed, the compatibility boundary should be obvious, tested, and scheduled for
removal.

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

New production Slang resources should use explicit scope attributes or a
compiler invocation default scope supplied by the caller that knows the shader
domain.

For Termin ABI resources, the canonical resource name is part of the authored
contract:

```slang
import termin_prelude;

[[TerminScope("pass")]]
ConstantBuffer<LightingBlock> lighting;

[[TerminScope("pass")]]
ConstantBuffer<ShadowBlock> shadow_block;

[[TerminScope("pass")]]
Sampler2DShadow shadow_maps[MAX_SHADOW_MAPS];
```

The compiler may recognize that a declared resource matches a documented Termin
ABI resource for validation and diagnostics. It must not infer ownership for
arbitrary names that merely resemble old engine resources, and it must not
assign special backend placement by resource name.

## Pass Authoring Contract

Pass and renderer code binds logical resources. It should know the ABI resources
that belong to the pass/material contract, but it should not know backend
placement:

```cpp
ctx.bind_uniform_data("per_frame", &per_frame, sizeof(per_frame));
ctx.bind_uniform_data("lighting", &lighting, sizeof(lighting));
ctx.bind_texture("shadow_maps", shadow_atlas, shadow_sampler);
ctx.bind_uniform_data("draw_data", &draw_data, sizeof(draw_data));
```

For Termin ABI resources, prefer typed helpers that centralize canonical names,
payload layout validation, and optional/required behavior:

```cpp
bind_frame_camera(ctx, camera);
bind_lighting(ctx, lighting);
bind_shadow_resources(ctx, shadows);
bind_material_resources(ctx, material);
bind_draw_transform(ctx, transform);
```

Those helpers may still call bind-by-name internally. Their purpose is to keep
canonical resource names, legacy alias handling, and struct packing contracts
out of unrelated pass logic.

Expected behavior:

- if a shader declares the canonical ABI resource with matching kind and scope,
  bind it by name;
- if a shader has layout metadata and omits an optional ABI resource, skip the
  bind without error;
- if a shader declares the resource with the wrong kind or scope, log an error
  and skip the bind;
- if a required ABI resource is missing, log an error in ABI terms;
- do not recover by trying a private list of old names in pass code.

## Compiler Contract

`termin_shaderc` is not the long-term owner of Termin backend placement policy.
It is responsible for producing artifacts and metadata that the runtime can turn
into a shader contract/layout. It should:

- read Slang reflection and Termin scope metadata;
- preserve missing scope metadata as `unscoped` until a policy resolves it;
- apply `--default-scope <frame|pass|material|draw|transient>` only to
  unscoped resources, without overriding explicit `TerminScope` metadata;
- reject invalid scope metadata for artifact-required migrated resources;
- validate documented Termin ABI names when enough metadata is available;
- avoid semantic ownership inference for arbitrary non-ABI resource names;
- while target compiler outputs still require numeric decoration/register
  patching, use only a transitional placement allocator based on generic
  contract data (`scope`, resource kind, stable logical name). This allocator is
  shared with the backend binding-plan layer, is compatibility glue rather than
  target architecture, and must not grow per-resource special cases such as
  `shadow_maps -> binding 8`;
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

Runtime may expose typed ABI helpers, but those helpers are thin validation and
binding frontends. They resolve to canonical shader resource names and must not
guess missing resources from numeric slots or unrelated strings.

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

Backend placement policy belongs in the backend binding planner. For migrated
paths the planner consumes shader contract/layout metadata and produces a
backend-specific binding plan. The compiler-side `set/binding` values in current
sidecars are transitional compatibility data until artifact generation can be
driven directly by that plan.

## Adding A Resource

When adding a new migrated shader resource:

1. Pick the semantic owner and scope.
2. Decide whether it is a local shader/pass resource or a Termin ABI resource.
3. If it is ABI, add it to the Termin Shader ABI table with kind, scope,
   producer, payload layout, optional/required behavior, and alias policy.
4. Pick a stable logical resource name.
5. Declare the resource in Slang with explicit Termin scope metadata.
6. Bind it from pass/material/draw code by logical name or typed helper.
7. Let `termin_shaderc` assign backend placement and write the sidecar.
8. Add validation or tests for missing name, kind mismatch, and relevant layout
   conflicts.
9. Update the relevant struct-layout documentation when the resource is a
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
