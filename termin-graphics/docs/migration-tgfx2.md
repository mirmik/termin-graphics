# Migration Plan: tgfx -> tgfx2

## Context

tgfx -- OpenGL-only graphics backend with mutable state-machine API.
tgfx2 -- backend-neutral API (OpenGL now, Vulkan later) with command buffer + pipeline model.

Goal: replace tgfx with tgfx2 across the engine while keeping the CPU-side resource
system (TcShader, TcMaterial, TcMesh, TcTexture), the asset system, hot-reload, and
the `.shader` file format fully intact. The migration is about swapping the GPU
backend under well-defined vtable boundaries, not rewriting the engine from scratch.

### Key architectural difference

tgfx (GL state machine):
```
backend->set_depth_test(false);
backend->set_blend(true);
shader.use();
shader.set_uniform("u_mvp", mvp);
texture->bind(0);
mesh->draw();
```

tgfx2 (pipeline + command buffer):
```
cmd->begin_render_pass(pass);
cmd->bind_pipeline(pipeline);        // all state baked in
cmd->bind_resource_set(resources);   // uniforms + textures via descriptor set
cmd->draw_indexed(count);
cmd->end_render_pass();
```

---

## Stage 0 findings (investigation, completed)

These facts are the load-bearing assumptions for every stage below. If any of
them changes, the plan must be revisited.

### 1. CPU-side resource system stays

TcShader, TcMaterial, TcMesh, TcTexture live in `termin-graphics` registries.
None of them stores GL IDs directly. GL state is held in `tc_gpu_slot` inside
`tc_gpu_share_group`, reached via `shader->pool_index`. TcShader itself owns
UUID, name, source_path, ref_count, version, features, and mutable sources
(`set_sources` bumps version, which drives per-context slot invalidation and
lazy recompile).

**This is the asset layer.** It already has identity, hot-reload, registry
lookup, ref-counting, and variant tracking. It stays.

### 2. The GL boundary is a single vtable, one file

All ~12 GL calls for shader compile/link/use/set_uniform live in
`termin-graphics/include/tgfx/opengl/opengl_backend.hpp` inside
`namespace gpu_ops_impl`, exposed via the `tgfx_gpu_ops` vtable. TcShader never
calls GL directly -- it goes through `ops->shader_compile/use/set_*`. The same
pattern holds for TcMesh (`gpu_ops_impl::mesh_*` owning VAO/VBO/EBO).

**Swapping the backend means rewriting one file.** No changes needed in
`tc_shader.c`, `tc_shader_registry.c`, `tc_mesh.c`, Python bindings, or any
pass code.

### 3. `.shader` format already has a typed uniform schema

`.shader` files use custom `@`-directives parsed by `shader_parser.cpp` (C++).
Uniforms are declared as:
```
@property Float u_strength = 0.5 range(0.0, 1.0)
@property Color u_tint = Color(1.0, 1.0, 1.0, 1.0)
@property Texture2D u_albedo = "white"
```
The parser produces `MaterialProperty{name, property_type, default_value, range}`
per phase. GLSL itself is stored raw, with individual `uniform` statements, and
no `layout(std140)` blocks exist anywhere today.

**This means std140 UBO blocks can be auto-generated from `@property` metadata
at shader load time.** No GL introspection, no SPIRV-Cross. Parser knows the
ordered list of (name, type, default); that is the block layout.

### 4. Uniform storage lives in TcMaterial, not TcShader

`tc_material_phase.uniforms[32]` is the mutable CPU-side cache -- name + type +
value union, populated by Python via `phase.set_uniform(...)`. TcShader's
`set_uniform_*` methods are **stateless wrappers over glUniform*** invoked only
by one function, `tc_material_phase_apply_uniforms` in `tc_gpu.c`, which loops
the cache and dispatches per-value.

**The single point of dispatch is that one function.** When it is replaced with
a UBO packer, every material pass migrates automatically.

### 5. LIGHTING_UBO is a working precedent

`TC_SHADER_FEATURE_LIGHTING_UBO` already drives a complete UBO path in
`color_pass.cpp` + `lighting_ubo.hpp` + `lighting.glsl`. Shader declares
`@features lighting_ubo`, parser emits the flag, ColorPass checks it and binds
a 672-byte UBO at slot 0. C-side struct (`LightDataStd140`) is hand-packed with
`alignas(16)` comments. No reusable std140 helper exists.

**Use as template; the new `MATERIAL_UBO` feature follows the same shape, but
the C struct and GLSL block are generated from `@property[]`, not hand-written.**

Known AMD quirk: shaders without the feature require explicit `ubo.unbind()` at
dispatch time. Must be replicated for material UBO.

### 6. Shader variants are almost a non-problem

Enum values exist for SKINNING / INSTANCING / MORPHING, but only SKINNING is
implemented. It creates a full new TcShader via GLSL string injection into the
VS source, keyed by original shader in a static `std::unordered_map<TcShader,
TcShader>`. ColorPass selects the variant per-drawcall on the collect phase
via `tc_component_override_shader`. Feature flags (`set_feature`) are pure
metadata bits -- they do NOT inject `#define` and do NOT trigger recompilation.

**Variants map cleanly onto tgfx2 PipelineCache** keyed by
`(shader_handle, vertex_layout, render_state)`. No permutation explosion.

### 7. Real immediate-mode `TcShader` sites are few and internal

Python code calling `TcShader.from_sources() + shader.use() + set_uniform_*`
directly (bypassing materials) is limited to:

- `framegraph/passes/present.py` -- one FSQ blit shader, one sampler
- `framegraph/passes/gizmo.py` -- view/proj/model matrices + color
- `visualization/render/system_shaders.py` -- pick shader, likely dead code
- `visualization/render/posteffects/*.py` + `postprocess.py` -- PostEffect
  framework + 6 effect subclasses (blur, gray, fog, bloom, highlight,
  material_effect). `PostProcessPass` is still instantiated from
  `editor_pipeline.py`, `project_browser.py`, `viewport.py`, but actual usage
  in real projects is uncertain. Migrate wholesale instead of investigating.
- `sdl_embedded.py` -- reuses `PresentToScreenPass._get_shader()`, not an
  independent site.

**All of these are internal to `termin-app`. No user-exported API depends on
immediate-mode TcShader.** Migration of these sites unblocks deletion of
`TcShader::use/set_uniform_*/compile/ensure_ready/gpu_program` entirely.

Framegraph passes like `skybox.py`, `color.py`, `material_pass.py`,
`shadow.py`, `tonemap.py`, `bloom_pass.py` go through
`TcMaterial.set_uniform_*`, which is the clean path -- they are not migration
work at the Python level, they piggyback on Stage 3.

### 8. Mesh vertex layout is data-driven already

`tgfx_vertex_layout` is a plain struct (`stride`, `attribs[8]` with
`name/size/type/location/offset`). Meshes select one of the canonical layouts
at load time (`pos`, `pos_normal_uv`, `pos_normal_uv_tangent`, `skinned`).
Attribute locations are baked into the descriptor and must match shader
`layout(location=N) in ...` declarations. VAO is owned by the
`OpenGLLayoutMeshHandle` GPU shadow, created in ctor via
`glVertexAttribPointer` per descriptor entry.

**Migration of TcMesh is parallel to TcShader**: rewrite `gpu_ops_impl::mesh_*`
to use tgfx2 buffers and feed the layout into `PipelineDesc.vertex_layouts`.
Layout descriptor on TcMesh becomes the contract, pipeline must agree.

---

## Stage 1 -- Public ResourceSet API + reference pass

**Goal**: A clean public tgfx2 API for binding uniform buffers and sampled
textures at draw time. Prove the end-to-end UBO path on GrayscalePass.

**Deliverables**:

- Public `RenderContext2::bind_resource_set(ResourceSetHandle)` or a narrower
  `bind_uniform_buffer(binding, buffer)` + `bind_sampled_texture(binding, tex,
  sampler)` surface. Decision based on what fits the existing
  `OpenGLCommandList::bind_resource_set` implementation, which already does
  `glBindBufferRange(GL_UNIFORM_BUFFER, ...)` + `glActiveTexture/glBindTexture`.
- `GrayscalePass::execute_tgfx2` stops using the `flush_pipeline()` +
  `glGetUniformLocation` + raw `glUniform*` escape hatch. The fragment shader
  declares `layout(std140, binding=0) uniform Params { float strength; };`
  and the pass creates + populates + binds the UBO via the new API.

**Out of scope**: material system, `@property` autogeneration, any other pass.

**Verification**: Grayscale cube renders; no raw GL in `grayscale_pass.cpp`;
no `flush_pipeline` call.

**Estimated**: 1 week.

---

## Stage 2 -- `@property` -> std140 UBO block generator

**Goal**: The `.shader` parser emits a GLSL `layout(std140) uniform MaterialBlock`
declaration from `@property[]`, computes std140 offsets, stores the layout on
`ShaderPhase` for later packing.

**Deliverables**:

- `shader_parser.cpp`: when a phase has `@features material_ubo`, iterate its
  ordered `MaterialProperty[]`, compute std140 alignment for each, emit GLSL
  source text for the block, prepend to both VS and FS source of that phase.
- `ShaderPhase::material_ubo_layout` -- vector of `{name, offset, size, type}`
  plus `block_size`.
- Strip or rename the original `uniform float u_name;` declarations in the raw
  GLSL for properties that are now in the block. Parser-level, not author-level.
- Decision: opt-in via `@features material_ubo`, not implicit. Legacy shaders
  without the flag continue to work unchanged.

**Out of scope**: packing actual values, binding at draw time.

**Verification**:

- Unit test with a fixture `.shader` containing mixed property types (Float,
  Vec3, Color, Mat4, Texture2D) -> compare generated GLSL against expected.
- Unit test that computed offsets match `glGetActiveUniformsiv` for the
  compiled program.
- Test std140 edge cases: `vec3 + float` packing, `mat3` alignment, arrays.

**Estimated**: 1-2 weeks.

---

## Stage 3 -- std140 packer + new apply path

**Goal**: Replace `tc_material_phase_apply_uniforms` for shaders with the
`MATERIAL_UBO` feature. Uniform values packed into a per-phase UBO, bound via
ResourceSet.

**Deliverables**:

- `std140_pack(layout, uniforms, out_buffer)` utility. One function, unit
  tested separately from material code.
- Per-phase `material_ubo: tgfx2::BufferHandle` lifecycle -- allocated on first
  apply, destroyed on phase destruction, **recreated on shader hot-reload when
  block_size changes**.
- `tc_material_phase_apply_via_resource_set(phase, shader, cmd_list)`:
  - reads layout from `shader->active_phase->material_ubo_layout`;
  - packs `phase->uniforms[]` into staging;
  - uploads to `phase->material_ubo`;
  - assembles `ResourceSetDesc` with the UBO + samplers from
    `phase->textures[]`;
  - calls `cmd_list->bind_resource_set`.
- Dispatch switch in `tc_gpu.c`: if
  `tc_shader_has_feature(MATERIAL_UBO)` call the new path, else fall back to
  legacy `tc_material_phase_apply_uniforms`.
- AMD unbind quirk replicated: for shaders without the feature in a context
  where the previous draw bound a material UBO at the same slot, emit an
  explicit unbind (pattern lifted from `color_pass.cpp:587-595`).

**Out of scope**: migrating any pass.

**Verification**:

- Unit test: construct a material with `@features material_ubo`, apply,
  `glGetBufferSubData` the UBO, compare bytes against expected.
- Hot-reload test: change `.shader` file to add/remove a property, confirm UBO
  is resized and packing remains correct.

**Estimated**: 1 week.

---

## Stage 4 -- First real pass on full tgfx2 path

**Goal**: One production framegraph pass drawing through
`ctx2->bind_pipeline/bind_vertex_buffer/draw` with material uniforms flowing
via Stage 3, end-to-end.

**Candidate**: `framegraph/passes/skybox.py`. Justification: it already uses
`material.set_uniform_vec3(...)` so uniform storage is already on the right
side; one shader; one draw call; minimal coupling to ColorPass infrastructure.
No dependency on immediate-mode TcShader (unlike present.py and gizmo.py).

**Deliverables**:

- Skybox `.shader` gains `@features material_ubo`.
- Skybox pass stops calling legacy `graphics->*` draw primitives, goes through
  `ctx.ctx2` for viewport, pipeline bind, resource set bind, draw.
- `RenderEngine` ensures `ctx2` is populated for this pass (already true per
  existing Phase 2 plumbing, confirm).

**Verification**:

- Scene with skybox renders identically to before.
- Hot-reload of `skybox.shader`, including adding a new `@property`, is picked
  up on the next frame without restart.

**Estimated**: 1-2 weeks (includes debugging PipelineCache hits, vertex layout
mismatch, AMD driver surprises).

---

## Stage 5 -- Material-pass migration wave

**Goal**: Migrate the remaining framegraph passes that already go through
TcMaterial. After this stage, all draw calls from material-backed passes go
through tgfx2 command lists.

**Passes in scope** (roughly simple -> complex):

1. `ShadowPass` -- single shader, depth-only target, output bound to shadow map.
2. `DepthPass`, `NormalPass`, `IdPass` -- mono-output utility passes.
3. `TonemapPass`, `BloomPass` -- post-process material passes (not to be
   confused with `posteffects/` Python framework).
4. `MaterialPass`.
5. `ColorPass` -- biggest. Main opaque + transparent draw loop, lighting UBO,
   skinning variant selection, instancing.

**Parallel sub-stage**: TcMesh vtable swap.

- Rewrite `gpu_ops_impl::mesh_*` to use `tgfx2::BufferHandle` for VBO/IBO.
- VAO is moved from the mesh shadow into `PipelineCache` (VAO per pipeline per
  vertex layout, keyed in the cache).
- Translate `tgfx_vertex_layout` -> `tgfx2::VertexBufferLayout` at bind time.
- Skinning variant uses the same mesh; only the shader differs, so vtable swap
  is orthogonal to variant logic.

**Out of scope**: immediate-mode Python passes (Stage 7).

**Verification**:

- Per-pass screenshot comparison against pre-migration baseline.
- Full scene render (cube + lit materials + shadows + skybox) identical.
- GPU timing not regressed beyond noise.

**Estimated**: 3-6 weeks. ColorPass is the dominant item.

---

## Stage 6 -- Swap TcShader and TcMesh backends

**Goal**: `gpu_ops_impl::shader_*` and `gpu_ops_impl::mesh_*` implementations
create resources through `tgfx2::IRenderDevice`, not raw GL. `tc_gpu_slot`
holds opaque tgfx2 handles instead of `uint32_t gl_id`.

**Deliverables**:

- `opengl_backend.hpp` -- rewrite the ~12 shader ops and the mesh ops to
  call `tgfx2::IRenderDevice::create_shader/create_buffer/...`. Legacy GL calls
  disappear from this file.
- `tc_gpu_slot` -- field type change from `uint32_t gl_id` to opaque handle
  (shader handle for shader slots, buffer handle for mesh slots).
- `TcShader::use/set_uniform_*` **are kept for now**. They still need to work
  for Stage 7 sites that haven't migrated yet. Under the hood they dispatch to
  the GL program id that the tgfx2 OpenGL backend happens to expose -- either
  via a backdoor getter, or via a temporary legacy path that coexists with the
  new one.

**Verification**: All passes migrated in Stages 4-5 still render. No GL calls
in registries, just vtable hops.

**Estimated**: 1 week.

---

## Stage 7 -- Migrate immediate-mode Python sites to material API

**Goal**: Eliminate every remaining `TcShader.from_sources/use/set_uniform_*`
call in Python by rewriting the corresponding site as a material-backed pass.

**Sites**:

1. `framegraph/passes/present.py` -> new `present.shader` file with
   `@property Texture2D u_tex`, FSQ vertex layout, pass draws through `ctx2`.
   `sdl_embedded.py` picks up the new shader automatically (or via a one-line
   fix to `PresentToScreenPass._get_shader()`).
2. `framegraph/passes/gizmo.py` -> new `gizmo_mask.shader` with
   `@property Mat4 u_view / u_projection / u_model` + `@property Color u_color`.
3. `visualization/render/system_shaders.py` -> the "pick" shader becomes a
   material. If `get_system_shader` truly has zero callers, delete the module
   instead.
4. `visualization/render/posteffects/*.py` + `postprocess.py` -- rewrite each
   `PostEffect` subclass (blur, gray, fog, bloom, highlight, material_effect)
   as a `.shader` file + material instance. `PostProcessPass` is rewritten to
   drive a chain of material applies via `ctx2`, not TcShader direct calls.
5. `editor_pipeline.py`, `project_browser.py`, `viewport.py` -- adjust
   `PostProcessPass` construction calls to the new API.

**Verification**:

- Every old user-facing behavior (screen blit, gizmo rendering, any
  post-process that was in use) continues to work.
- `grep -r "TcShader.from_sources\|shader\.use(\|shader\.set_uniform" termin-app/`
  returns zero hits in non-test code.

**Estimated**: 2-3 weeks. `posteffects/` is the bulk.

---

## Stage 8 -- Cleanup

**Goal**: Remove the legacy API surface now that nothing calls it.

**Deliverables**:

- Delete `tc_material_phase_apply_uniforms` (legacy glUniform path in `tc_gpu.c`).
- Delete `TcShader::use/set_uniform_int/float/vec*/mat4/set_block_binding/
  compile/ensure_ready/gpu_program` from both C++ API and Python bindings.
- Delete `gpu_ops_impl::shader_use/set_*` and `gpu_ops_impl::shader_compile`'s
  GL-specific branch (it now forwards to tgfx2 only).
- Remove `GraphicsBackend*` from `ExecuteContext`, delete `OpenGLGraphicsBackend`,
  `OpenGLFramebufferHandle`, `tgfx2_gpu_ops.cpp` (the Phase 0 adapter that
  routes legacy calls to tgfx2), legacy `tgfx_gpu_ops.h` where still present.
- `FBOMap` dropped from `ExecuteContext`, only `Tex2Map` remains.
- Rename `tgfx2` -> `tgfx`. Old `tgfx` directory is already empty by this point.

**Verification**: Build clean with no legacy includes, full test suite passes,
manual smoke test of the editor.

**Estimated**: 1-2 weeks.

---

## Risks

1. **std140 packing edge cases.** vec3 + float alignment, mat3 layout, array
   stride. Unit tests in Stage 2 must cover these explicitly. Bad packing
   produces silent visual corruption.
2. **Hot-reload with layout change.** If a user adds a `@property` in the
   editor, the UBO size changes. Stage 3 must destroy and recreate the
   per-phase UBO on version bump, and invalidate any cached `ResourceSetHandle`.
3. **AMD UBO unbind.** The driver quirk from `color_pass.cpp:592` applies to
   material UBO as well. Must be in the dispatcher from day one, not patched
   in later.
4. **ColorPass volume.** Stage 5's biggest item. Not complex per variant, but
   large body of code: collect loop, sort, draw loop, lighting UBO, skinning
   variant override, instancing. Allocate a dedicated sub-stage with its own
   screenshot tests.
5. **`PostProcessPass` external subclasses.** If any downstream project has
   user-written `PostEffect` subclasses, Stage 7 breaks them. Options: ship
   deprecation warnings in Stage 7, or document as a breaking change.
6. **`system_shaders.py` real callers.** Module looks dead; if it is not, Stage
   7 scope grows slightly. Confirm in Stage 7 before deleting.

## Ordering notes

- Stages 1-3 are sequential (foundation).
- Stage 4 must complete before Stage 5.
- Stages 5 and 7 can be done in parallel by two people (different parts of the
  codebase). Stage 5 is C++, Stage 7 is Python + new `.shader` files.
- Stage 6 must happen after Stage 5 but can happen before or in parallel with
  Stage 7's completion.
- Stage 8 is strictly last.

## Total estimate

3-4 calendar months with one person, 2-2.5 months with two people parallelising
Stages 5 and 7.
