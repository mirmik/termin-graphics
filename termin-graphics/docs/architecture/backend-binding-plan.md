# tgfx2 Backend Binding Plan

Дата: 2026-06-21

Этот документ фиксирует текущий live contract между shader resource metadata,
`RenderContext2` и backend command lists. Исторический план миграции находится
в `docs/plans/2026-06-21-tgfx2-backend-binding-plan-layer.md`.

## Contract

Основной путь для migrated shader/material code:

```text
shader semantic resources
  -> BackendBindingPlan
  -> BoundResourceSetDesc
  -> backend-native command execution
```

Общими между backend-ами остаются только semantic resource facts:

- resource name;
- kind: constant buffer, texture, sampler, storage buffer, storage texture;
- scope: frame, pass, material, draw, transient, unscoped;
- stage mask;
- resource size / array count where known.

Backend placement не является одним общим numeric slot. Placement хранится в
`BackendBindingPlanEntry::placement` как backend-specific payload:

- Vulkan: descriptor set, descriptor binding, descriptor kind;
- D3D11: register class `b/t/s/u` and register index;
- OpenGL: UBO/SSBO/image binding point or texture/sampler unit.

Backend command lists must not infer placement from resource names. They consume
the placement payload selected for their backend.

## Public Types

`BackendBindingPlan` is built from shader metadata for one active backend. Each
entry contains:

- `ShaderResourceKey resource`;
- `stage_mask`;
- `array_count`;
- `size`;
- `BackendPlacement placement`.

`BoundResourceSetDesc` is the backend boundary for migrated paths. It contains:

- `resource_layout_token`: the current pipeline resource layout identity;
- `groups`: scope-preserving `BoundResourceGroup` entries for frame/pass/
  material/draw/transient bindings;
- `bindings`: transitional flat compatibility entries used only when `groups`
  is empty.

Each `BoundResourceGroup` contains:

- `scope`: the shader resource scope preserved to the backend boundary;
- `dirty`: whether this scope changed since the last emitted resource set for
  the current pass/pipeline;
- `bindings`: pairs of `BackendBindingPlanEntry` and `BoundResourceValue`.

`BoundResourceValue` contains only values:

- buffer / texture / sampler handles;
- offset / range;
- array element.

It must not carry backend placement. Placement is always read from the matching
`BackendBindingPlanEntry`.

## Backend Responsibilities

### Vulkan

Vulkan maps `resource_layout_token` to the pipeline's descriptor set layout
internals. `VulkanRenderDevice::create_bound_resource_set()` resolves each
binding through `placement.vulkan` and writes descriptors from the corresponding
`BoundResourceValue`.

Rules:

- descriptor set is currently restricted to `0`;
- descriptor binding comes from `placement.vulkan.binding`;
- descriptor kind must match the reflected descriptor layout;
- dynamic UBO offsets are emitted through `VkResourceSetResource`, not baked into
  descriptor identity.
- Vulkan consumes all groups, including clean groups, because descriptor set
  creation still needs a complete descriptor state.

### D3D11

D3D11 has no descriptor sets. `D3D11CommandList::bind_resource_set()` applies
`placement.d3d11` directly:

- `b#`: constant buffers;
- `t#`: shader resource views;
- `s#`: samplers;
- `u#`: UAV/storage resources when supported.

Backend-specific lowering decisions belong in placement metadata. For example,
Slang D3D11 comparison sampler arrays may be legalized to one scalar sampler
while the texture remains arrayed; this is represented by
`placement.d3d11.scalar_sampler_for_texture_array`, not by recognizing resource
names such as `shadow_maps`.

Stage visibility comes from the plan entry's stage mask.
When `BoundResourceSetDesc::groups` is present, clean groups are skipped so
unchanged scopes do not rebind native D3D11 slots.

### OpenGL

OpenGL has no descriptor sets. `OpenGLCommandList::bind_resource_set()` applies
`placement.opengl` directly:

- UBO/SSBO/image binding points;
- texture units;
- sampler units.

The current transitional OpenGL texture-unit ranges are scope-first and
non-overlapping: frame `0..3`, material `4..19`, pass `20..27`, draw `28..31`,
and transient `32..47`. The material range intentionally covers
`TC_MATERIAL_MAX_TEXTURES` so generated PBR-style material shaders do not
overflow into pass or transient texture units.

Image bindings currently log unsupported diagnostics unless implemented by a
specific path.
When `BoundResourceSetDesc::groups` is present, clean groups are skipped so
unchanged scopes do not rebind native OpenGL slots.

## RenderContext2 Responsibilities

`RenderContext2` resolves symbolic resources through the active shader layout:

```text
resource name -> BackendBindingPlanEntry -> BoundResourceValue
```

It then calls `IRenderDevice::create_bound_resource_set()`. Migrated symbolic
paths must not construct backend placement themselves and must not rely on
`ResourceBinding::set/binding`.

Pending symbolic resources are bucketed by shader scope and emitted as
`BoundResourceGroup` entries. Backends therefore receive scope information
without inferring it from names or numeric slots.
`RenderContext2` tracks dirty scopes separately from desired binding state and
marks all scopes dirty when pass or pipeline resource layout changes.
Repeated symbolic binds with the same resolved value do not dirty the scope or
recreate the current resource set.

Numeric APIs such as `bind_uniform_buffer(uint32_t binding, ...)` remain as an
explicit legacy/low-level side channel. They are carried as
`legacy_numeric_bindings` beside planned bindings during the migration.

## Legacy Numeric API

`ResourceBinding` and `ResourceSetDesc` are compatibility types for callers that
already know numeric backend placement. They are not the preferred shader
resource contract.

Allowed uses:

- low-level tests and backend smoke tests that intentionally bind numeric slots;
- compatibility paths for custom/unported backends;
- transitional numeric APIs on `RenderContext2`.

New migrated shader/material code should bind by semantic resource name and use
`BackendBindingPlan` / `BoundResourceSetDesc`.

## Validation

`build_backend_binding_plan()` validates resource metadata before command
execution:

- empty names, invalid kinds, and empty stage masks are rejected;
- duplicate semantic names with incompatible kind/scope are rejected;
- D3D11 missing placement, register-class mismatch, and stage/register conflicts
  are rejected;
- OpenGL binding point / texture unit conflicts are rejected;
- Vulkan non-zero descriptor sets and duplicate `(set,binding)` conflicts are
  rejected.

Backends also validate at resource-set creation time where layout information is
required. For example, Vulkan rejects wrong backend placement kind,
descriptor-kind mismatch, value-kind mismatch, missing layout binding, and
unsupported descriptor sets.

## Transitional State

The live contract is bound-first and scope-preserving, but two pieces are still
transitional:

- `BoundResourceSetDesc::bindings` remains as flat compatibility input for old
  tests and custom/unported backends. Concrete tgfx2 backends consume
  `BoundResourceSetDesc::groups` when present.
- Scope groups preserve structure at the backend boundary, and OpenGL/D3D11 use
  dirty flags to avoid rebinding clean scopes. Vulkan still processes the full
  grouped state to create a complete descriptor set.
- `pipeline_resource_layout_token()` is an opaque token. A named
  `PipelineResourceLayout` handle may replace it once layout ownership is
  formalized.
