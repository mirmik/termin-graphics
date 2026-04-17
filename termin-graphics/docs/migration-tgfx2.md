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

---

## Progress & remaining work (2026-04-17)

### Done

- **Stages 0-7** — all complete. Every render pass runs on `ctx2`; tcgui /
  tcplot / nodegraph ported; Python `TcShader` immediate-mode usage gone;
  `.shader` + material UBO path is the default.
- **Stage 8.1** — legacy `TcShader::use/set_uniform_*/compile/ensure_ready/
  gpu_program/set_block_binding` + `tc_shader_*_gpu` + `gpu_ops_impl::shader_*`
  removed.
- **Stage 8.2** — `tc_material_phase_apply_uniforms` removed.
- **Stage 8.3** — `FBOPool` on native `tgfx2::TextureHandle` via
  `ensure_native(device, key, …)`; ShadowPass on native depth textures with
  D24 + CLAMP_TO_BORDER + REF_TO_TEXTURE/LEQUAL for hardware PCF;
  `ShadowMapArrayEntry.depth_tex2` replaces legacy `FramebufferHandle*`;
  picking rewritten to `pipeline.get_color_tex2("id") + read_pixel_rgba8`.
- **Stage 8.4** — `ExecuteContext` no longer has `graphics/reads_fbos/
  writes_fbos`; shadow arrays pass through `ctx.shadow_arrays`;
  `FrameDebuggerCallbacks` + `maybe_blit_to_debugger` deleted.
- **Stage 8.5** — `invalidate_fbo_cache()` moved into
  `FBOPool::ensure_native` on resize/recreate.
- **Stage 8.6** — **Python surface fully cleaned, C++ classes still alive
  but unused**:
  - `FrameGraphCapture` / `FrameGraphPresenter` on native textures;
    `OpenGLRenderDevice::read_texture_rgba_float` / `read_texture_depth_float` /
    `blit_to_external_fbo` / `clear_external_fbo` / `gl_texture_id` added.
  - `UIRenderer` owns native color+depth attachments; composite via
    `ctx.blit_to_external_fbo(0, color_tex, …)`. `UIRenderer.__init__(font=None)`
    and `UI.__init__(font=None)` — `graphics` dropped.
  - C++ `SDLWindow` / `SDLWindowRenderSurface` stripped of
    `OpenGLGraphicsBackend*` + `FramebufferHandlePtr` + `set_graphics` +
    `get_window_framebuffer`. Duplicate headers under
    `termin-app/cpp/termin/platform/sdl_*` deleted.
  - `RenderingManager::set_graphics` + `graphics_` + `graphics()` gone.
    `PullRenderingManager` same.
  - `EditorInteractionSystem::set_graphics` + `_graphics` (never read) gone.
  - `RenderEngine(GraphicsBackend*)` constructor + `graphics` field gone.
    `ensure_tgfx2()` no longer gates on `graphics`.
  - `render_view_to_fbo(FramebufferHandle*)` (~358 LoC) deleted; new
    `render_view_to_fbo_id(uint32_t target_fbo_id, …)` keeps internal
    color+depth attachments and blits to external GL fbo id.
  - `ViewportRenderState` / `ViewportContext` now own native
    `(output_color_tex, output_depth_tex)` pair instead of legacy
    `FramebufferHandlePtr`; OUTPUT/DISPLAY go straight into `ctx.tex2_writes`
    in `RenderEngine::render_view_to_fbo_id` +
    `render_scene_pipeline_offscreen`.
  - posteffects (`bloom.py`, `postprocess.py`) use native
    `ctx2.create_color_attachment` (new `RenderContext2.create_color_attachment`
    binding) for their mip chain / ping-pong temp attachments.
  - `Tgfx2ContextHolder` constructor now runs
    `tc_ensure_default_gpu_context()` + `tgfx2_interop_set_device` +
    `tgfx2_gpu_ops_register()` so **every standalone Python host works
    without `OpenGLGraphicsBackend.get_instance().ensure_ready()`** — that
    call is gone from launcher, both editor entry points, tcplot /
    termin-gui / termin-nodegraph examples, diffusion-editor, physics demo.
  - Diffusion-editor GPU compositor (`gpu_compositor.py`, ~380 LoC) fully
    rewritten on tgfx2 (own holder, native main / display / temp-pool
    attachments, `TcShader.from_sources + tc_shader_ensure_tgfx2`, immediate
    quad draw, `device.read_texture_rgba_float` readback). Cross-device
    texture handoff to `UIRenderer` via `get_gl_id()` +
    `wrap_gl_texture_as_tgfx2` (different holders, same GL context).
  - Python surface: `termin/graphics/__init__.py` no longer re-exports
    `GraphicsBackend`/`OpenGLGraphicsBackend`/`FramebufferHandle`/
    `GPUTextureHandle`/`ShaderHandle`/`GPUMeshHandle` — only `RenderState`,
    `Color4`, `RenderSyncMode` are kept (navmesh/voxels debug overlays).
    `backends/__init__.py` collapsed to SDL-embedded only; Python stubs
    `nop_graphics.py`, `nop_window.py`, `glfw.py`, `opengl.py` deleted.
    Dead tests / orphan files (`passes_test.py`, `gizmo_immediate.py`,
    `sdl_backend.py`, `platform/window.py`, `backends/sdl.py`, `backends/qt.py`)
    deleted.
  - SWIG: `RenderingManager.set_graphics`, `PullRenderingManager.set_graphics`,
    `RenderEngine(GraphicsBackend*)` / `RenderEngine::graphics` stripped
    from `termin.i`.
  - Examples restored and migrated (5 tcplot, 7 termin-gui, 1 termin-nodegraph,
    1 root `sdl_cube.py` rewritten from scratch on `render_view_to_fbo_id(0, …)`).

### Remaining

**Stage 8.6 final — delete the dead C++ classes** (probably one session):

1. Drop nanobind binding sections in
   `termin-graphics/python/bindings/graphics_bindings.cpp` for
   `GraphicsBackend`, `OpenGLGraphicsBackend`, `FramebufferHandle`. Keep
   `ShaderHandle` / `GPUMeshHandle` / `GPUTextureHandle` / `TcShader` /
   `Color4` / `Size2i` / `Rect2i` / `PolygonMode` / `BlendFactor` / `DepthFunc`
   / `RenderState` / `DrawMode` / `init_opengl` — those are still used by
   `tmesh`, navmesh, voxels debug overlays, and tgfx2 state structs.
2. Delete class files:
   - `termin-graphics/include/tgfx/graphics_backend.hpp`
   - `termin-graphics/include/tgfx/opengl/opengl_backend.hpp` (**big — also
     contains the `gpu_ops_impl::register_gpu_ops` inline functions; they go
     in Stage 8.7**)
   - `termin-graphics/src/opengl/opengl_backend.cpp`
   - `termin-graphics/src/opengl/opengl_backend_singleton.cpp`
   - `termin-graphics/include/tgfx/opengl/opengl_framebuffer.hpp`
   - FramebufferHandle entries in `termin-graphics/include/tgfx/handles.hpp`
3. SWIG: remove `FramebufferHandle` class from `termin-csharp/termin.i` +
   drop `RenderPipeline::get_fbo(name) -> FramebufferHandle*` export.
4. Fix the resulting compile errors (expected: `RenderPipeline::get_fbo` is
   the only C++ core user; everything else has been cleaned).

**Stage 8.7 — `tgfx2_gpu_ops` forwarder + `tc_gpu_slot`**:

- Before Stage 8.1 the `tgfx_gpu_ops` vtable had two implementations: raw-GL
  in `opengl_backend.hpp::gpu_ops_impl::register_gpu_ops` (installed by
  `ensure_ready`) and tgfx2-backed in `tgfx2_gpu_ops.cpp::tgfx2_gpu_ops_register`
  (installed by `RenderEngine::ensure_tgfx2` and, since 2026-04-17, by
  `Tgfx2ContextHolder` ctor). The raw-GL one has no remaining callers —
  **delete it** and the forwarder layer becomes trivial / can be inlined.
- `tc_gpu_slot` was a cache that backed `TcShader.use() / set_uniform_*` —
  those are gone since Stage 8.1 so the cache is unread. Delete.
- `tgfx2_interop_set_device` gets folded or removed once the slot system goes.

**Stage 8.8 — rename `tgfx2 → tgfx`**:

- `tgfx2::` → `tgfx::` across all source.
- `include/tgfx2/` directory → `include/tgfx/` (the legacy `tgfx/` should be
  empty after 8.6/8.7).
- Python bindings: `Tgfx2Context`/`Tgfx2RenderContext`/`Tgfx2TextureHandle`/
  `Tgfx2BlendFactor`/`Tgfx2PixelFormat`/etc. → `TgfxContext` / `TgfxRenderContext`
  / `TgfxTextureHandle` / `TgfxBlendFactor` / `TgfxPixelFormat`.
- CMake: `termin_graphics2` library → `termin_graphics`.
- C# SWIG: regenerate after rename.

### Post-migration (not Stage 8)

- **Vulkan backend**: `device_factory.cpp::create_device(BackendType)` +
  `VulkanRenderDevice` already exist behind `TGFX2_HAS_VULKAN`. Two
  hardcodes to remove: `Tgfx2ContextHolder` ctor and
  `RenderEngine::ensure_tgfx2` both `std::make_unique<OpenGLRenderDevice>()`
  directly. Also generalise the OpenGL-specific methods on `RenderContext2`
  (`blit_to_external_fbo` / `wrap_gl_texture` / `clear_external_fbo` — all
  `dynamic_cast<OpenGLRenderDevice*>`) either via an abstract `present/clear`
  API or by routing everything through `begin_pass(native_texture)`. Plus
  GLSL → SPIR-V compile step (glslang / shaderc) for Vulkan.
- **#97 chronosquad ShadowPass crash** — driver-level SIGSEGV after the
  gpu_ops vtable swap. Not a blocker; deferred for post-Stage-8 debug.

### Verified working on 2026-04-17

Launcher (sdk/bin/termin_launcher), Qt editor (both launcher-spawned and
directly), tcgui editor, diffusion-editor, tcplot examples (demo_sin,
demo_multi, demo_scatter, demo_3d_helix, demo_3d_surface), termin-gui
examples (sdl_hello / sdl_tree / sdl_showcase / sdl_canvas_demo / etc.),
termin-nodegraph demo, physics demo, root `sdl_cube.py`.

### Build-system quirks to remember

- `install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tgfx/)` in
  `termin-graphics/python/CMakeLists.txt` used to overwrite fresh
  `_tgfx_native.so` with a stale source-tree copy; fixed with
  `PATTERN "*.so" EXCLUDE`. Watch other `python/CMakeLists.txt` for the same
  pattern.
- After modifying tgfx2 bindings, sometimes `cmake --build` doesn't rebuild
  the .o; use `cmake --build . --clean-first` once if a newly-added symbol
  is missing.
- Pip wheel version is derived from max mtime of `$TERMIN_SDK/lib/python/**/*.so`
  (`TerminCMakeBuildExt.compute_local_version`). Without an explicit
  `cmake --install .` between build and `install-pip-packages.sh`, the
  cached wheel wins and ships stale .so.
