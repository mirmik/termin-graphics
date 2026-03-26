# Migration Plan: tgfx -> tgfx2

## Context

tgfx -- OpenGL-only graphics backend with mutable state machine API.
tgfx2 -- backend-neutral API (OpenGL + Vulkan) with command buffer + pipeline model.

Goal: replace tgfx with tgfx2 across the engine, reusing existing resource registries
and enabling gradual migration of ~20 render passes and ~80 consumer files.

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
cmd->bind_pipeline(pipeline);       // all state baked in
cmd->bind_resource_set(resources);   // uniforms + textures via descriptor set
cmd->draw_indexed(count);
cmd->end_render_pass();
```

### What can be reused

CPU-side resource registries (tc_texture, tc_shader, tc_mesh, tc_material) store NO GL IDs.
GPU state is separated into tc_gpu_share_group via pool_index lookup.
The vtable (tgfx_gpu_ops) decouples registries from GPU backend.

- Registries: REUSE as-is
- GPU context/share_group: ADAPT (tc_gpu_slot to hold tgfx2 handles)
- Vtable implementations: REWRITE to call tgfx2 IRenderDevice
- Material system: REUSE (apply_gpu needs new vtable impl)

---

## Phase 0: tgfx2-backed GPU Ops (no consumer changes)

**Goal**: Resource upload/compile/bind goes through tgfx2::IRenderDevice instead of raw GL,
while all consumer code remains unchanged.

**Steps**:
1. Adapt `tc_gpu_slot` -- `gl_id: uint32_t` -> union/extension for tgfx2 handle
2. Write new `tgfx_gpu_ops` vtable implementation that calls IRenderDevice methods
   (create_texture, create_shader, create_buffer, etc.)
3. Make two vtable implementations selectable at init: `opengl_gpu_ops` (legacy)
   and `tgfx2_gpu_ops` (new)

**NOT touched**: uniform setters (keep direct GL), render passes, GraphicsBackend

**Files**:
- `include/tgfx/tc_gpu_share_group.h` -- tc_gpu_slot adaptation
- `include/tgfx/tgfx_gpu_ops.h` -- vtable definition
- `src/` -- new tgfx2-backed vtable implementation

**Verification**: Existing render produces identical output through both vtable paths.

---

## Phase 1: RenderContext2 -- new mid-level layer

**Goal**: Create abstraction that replaces GraphicsBackend for render passes.

**Key idea -- PipelineCache**: hash of {shader, vertex_layout, render_state, color_format,
depth_format} -> PipelineHandle. Lazily creates pipelines. This bridges state-machine
mental model to pipeline model.

**RenderContext2 API**:
- Owns `ICommandList`
- Stores "pending" render state (depth, blend, cull) as mutable fields
- `begin_pass(color_tex, depth_tex, clear)` -> `cmd->begin_render_pass()`
- `end_pass()` -> `cmd->end_render_pass()`
- `set_depth_test()`, `set_blend()`, etc. -- change pending state
- `bind_shader(handle)` -- remember current shader
- `bind_texture(unit, texture)` -- remember binding
- `draw()` / `draw_fullscreen_quad()` / `draw_immediate_lines()` -- hash state,
  lookup/create pipeline, execute draw
- `blit(src, dst)` -- copy/blit

**Files** (new):
- `include/tgfx2/render_context.hpp`
- `include/tgfx2/pipeline_cache.hpp`
- `src/tgfx2/render_context.cpp`
- `src/tgfx2/pipeline_cache.cpp`

**Verification**: Standalone test -- create RenderContext2, draw triangle, check pixels
(similar to vulkan_smoke_test).

---

## Phase 2: Dual-path ExecuteContext -- gradual pass migration

**Goal**: Add `RenderContext2*` to ExecuteContext alongside `GraphicsBackend*`,
migrate simple passes one at a time.

**Migration order** (simple to complex):
1. `GrayscalePass` -- ~15 lines, fullscreen quad + shader
2. `PresentToScreenPass` -- blit framebuffer
3. `TonemapPass` -- fullscreen quad + shader
4. `BloomPass` -- multi-pass with mip chain
5. `GroundGridPass` -- single shader, single draw call

**Pattern**:
```
// OLD:
ctx.graphics->set_depth_test(false);
ctx.graphics->bind_framebuffer(output_fbo);
shader_.use();
input_tex->bind(0);
ctx.graphics->draw_ui_textured_quad();

// NEW:
ctx.ctx2->begin_pass(output_tex, clear);
ctx.ctx2->set_depth_test(false);
ctx.ctx2->bind_shader(shader_handle);
ctx.ctx2->bind_texture(0, input_texture);
ctx.ctx2->draw_fullscreen_quad();
ctx.ctx2->end_pass();
```

**Files**:
- `termin/cpp/termin/render/execute_context.hpp` -- add `RenderContext2* ctx2`
- `termin-render/` -- RenderEngine fills both pointers
- `termin/cpp/termin/render/grayscale_pass.cpp` -- first migrated pass
- (and so on)

**Verification**: After each pass migration, render output is identical.

---

## Phase 3: Framebuffer -> tgfx2 render targets

**Goal**: Replace FramebufferHandle (OpenGL FBO) with tgfx2 texture-based attachments.

**Steps**:
1. `Tgfx2FramebufferHandle` -- new implementation holding `tgfx2::TextureHandle`
   for color/depth
2. `bind_framebuffer()` -> `begin_render_pass()` internally
3. `blit_framebuffer()` -> `copy_texture()`
4. `read_pixel()` -> staging buffer + `read_buffer()`
5. FBOPool adapts to new handles

**Files**:
- `include/tgfx/opengl/opengl_framebuffer.hpp` -- replacement
- `termin-render/include/termin/render/fbo_pool.hpp`
- `termin-render/include/termin/render/frame_pass.hpp`

**Verification**: Shadow maps, MSAA resolve, pixel readback all work.

---

## Phase 4: ColorPass + Material System

**Goal**: Migrate main render pass and material system to tgfx2.

**Steps**:
1. `ColorPass::execute_with_data()` -> through RenderContext2
2. Material phase apply: pack uniforms into UBO, upload via `upload_buffer()`,
   bind via `ResourceSetHandle`
3. `TcShader` wrapper -> tgfx2 path (compile creates tgfx2::ShaderHandle)
4. Shader variants (skinning/instancing/morphing) -> pipeline variants via PipelineCache
5. GLSL -> SPIR-V: runtime compilation through shaderc (already in the project)
6. Remaining passes: ShadowPass, IdPass, ColliderGizmoPass, ImmediateRenderer,
   SolidPrimitiveRenderer

**Files**:
- `termin/cpp/termin/render/color_pass.cpp`
- `termin-graphics/src/tgfx_resource_gpu.c` -- material apply via tgfx2
- `termin/cpp/termin/render/shadow_pass.cpp`, `id_pass.cpp`, etc.

**Verification**: Full scene with materials, shadows, transparency. GL vs Vulkan comparison.

---

## Phase 5: Python bindings

**Goal**: Update Python-exposed APIs.

- Add bindings for RenderContext2
- Python passes receive `ctx.ctx2`
- Deprecation warnings on old GraphicsBackend API
- Material bindings unchanged (C-level struct not affected)

---

## Phase 6: Legacy removal

- Remove `GraphicsBackend*` from ExecuteContext
- Delete OpenGLGraphicsBackend, OpenGLFramebufferHandle, OpenGLShaderHandle
- Delete legacy vtable implementation
- `tc_gpu_slot` -> tgfx2 handles only
- Remove VAO management, share group GL-specific arrays

---

## Risk and ordering

| Phase | Risk   | Rationale                                                    |
|-------|--------|--------------------------------------------------------------|
| 0     | Low    | No consumer changes, output comparison validates correctness |
| 1     | Low    | Isolated new abstraction, standalone tests                   |
| 2     | Medium | Each pass migrated separately, legacy fallback available     |
| 3     | Medium | Framebuffers used everywhere, but abstraction hides details  |
| 4     | High   | Material system + main pass, most complex                    |
| 5     | Low    | Bindings are wrappers, main work already done                |
| 6     | Low    | Cleanup, all consumers already migrated                      |
