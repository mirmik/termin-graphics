# Migration Plan: tcgui / tcplot Python → tgfx2

Companion to `migration-tgfx2.md`. That document describes the C++-side
migration (render passes, TcMaterial, TcMesh). This one scopes the
Python-side migration for `tcgui` and `tcplot`, driven by the need to
eventually port `tcplot` to C++ with C# bindings.

## Context

Two Python libraries still talk to tgfx1:

- **`tcgui`** (`termin-gui/python/tcgui/`) — widget framework. `UIRenderer`
  draws through tgfx1 (`draw_ui_vertices`, `draw_ui_textured_quad`,
  `TcShader`, `enable_scissor`, `create_texture`). Main consumer:
  `diffusion-editor`.
- **`tcplot`** (`tcplot/tcplot/`) — plotting library. `PlotEngine2D`
  draws through a duck-typed context (provided by tcgui `UIRenderer` at
  runtime). `PlotEngine3D` uses tgfx1 directly (`TcShader`, `TcMesh`,
  `OpenGLGraphicsBackend`, `tgfx.text3d.Text3DRenderer`).

The end goal is a C++ `tcplot` with C# bindings. The user requirement is
that the **Python engine** runs fully on tgfx2 **before** the C++ port
begins. That forces the tcgui migration to happen first, since tcgui
owns the render context that tcplot's widgets draw through.

## Consumer impact audit

Before planning changes, the blast radius of a tcgui migration:

| Consumer | File:line | Coupling | Breakage risk |
|---|---|---|---|
| `diffusion-editor` | `main.py:8,161`, `editor_window.py:76,128-1352` | Thin client — imports `OpenGLGraphicsBackend` once, passes to `EditorWindow`, composes high-level widgets. No Widget subclassing, no direct rendering. ~400-500 LoC of tcgui composition. | **LOW** — if `UI(graphics)` + `UIRenderer` public API stay byte-compatible, no changes required |
| `tcplot` | `tcplot/tcplot/plot2d.py`, `plot3d.py` | Subclasses `Widget`, uses `MouseEvent` / `MouseWheelEvent`. `Plot2D` renders through the duck-typed ctx supplied by `UIRenderer`. `Plot3D` currently bypasses `UIRenderer` for 3D and talks to tgfx1 directly. | Widget adapters unaffected if `UIRenderer` contract holds; `PlotEngine3D` must migrate explicitly |
| tcgui tests | `termin-gui/python/tests/` | 11 layout tests (test_hstack_layout, test_scroll_area, test_diffusion_editor_layout, ...). No rendering tests — no GL context in CI. | Safety net for layout only; zero coverage for draw correctness |

**Conclusion:** the migration can preserve all consumer contracts.
`tcgui`'s `UIRenderer` has a stable 15-method public surface that is the
only thing above the tgfx1 boundary. Everything else — widgets,
diffusion-editor, `PlotEngine2D` — sits above that surface and does not
touch tgfx1 directly.

## Architectural approach: tgfx2 hidden inside `UIRenderer`

```
BEFORE:
  UI(graphics: tgfx1.GraphicsBackend)
    └── UIRenderer.draw_*  →  tgfx1.draw_ui_vertices / draw_ui_textured_quad / TcShader
         └── GL state (manual: enable_scissor, set_depth_test, ...)

AFTER:
  UI(graphics: tgfx1.GraphicsBackend)            ← signature unchanged
    └── UIRenderer.draw_*  →  ctx2.draw_immediate_triangles / Text2D / bind_sampled_texture
         └── RenderContext2 created in begin(), closed in end()
```

The public `UIRenderer` API (15 methods: `begin`/`end`/`draw_rect`/
`draw_rect_outline`/`draw_line`/`draw_text`/`draw_text_centered`/
`draw_image`/`upload_texture`/`load_image`/`measure_text`/`begin_clip`/
`end_clip`/`font` property/constructor) stays identical. Widgets,
diffusion-editor, tcplot `PlotEngine2D` are unaffected by the internal
tgfx1 → tgfx2 swap.

`PlotEngine3D` is a separate matter — it currently bypasses `UIRenderer`
for the 3D scene and talks to tgfx1 directly. It migrates in a dedicated
phase after `UIRenderer` is done and can expose its internal ctx2 to
child widgets.

## Missing Python bindings (blockers)

The tgfx2 Python binding surface (`termin-graphics/python/bindings/
tgfx2_bindings.cpp`) is currently a minimum for C++-driven render pass
callbacks. Python cannot yet:

- Construct a `RenderContext2` (`nb::class_` has no `nb::init`, line 74).
- Create or upload textures through tgfx2 (no `IRenderDevice::create_texture`
  / `upload_texture` exposed).
- Call `draw_immediate_triangles` / `draw_immediate_lines` (implemented
  in C++ at `render_context.cpp:563,593` but not bound).
- Wrap an arbitrary GL texture as a tgfx2 handle (only
  `wrap_fbo_color_as_tgfx2` exists).
- Set scissor through tgfx2 from Python.

These gaps are the subject of Phase 0.

## The Plan

### Phase 0 — tgfx2 Python bindings foundation *(2-3 days)*

C++ work in `termin-graphics/python/bindings/tgfx2_bindings.cpp`.

1. **`RenderContext2` factory.** A Python-callable helper, e.g.
   `tgfx2.make_context(graphics)`, that accepts an
   `OpenGLGraphicsBackend`, constructs an `IRenderDevice` +
   `ICommandList` over the same GL context, and returns a
   `Tgfx2RenderContext`. This is the linchpin — without it no Python
   code in a standalone SDL window can obtain a ctx2.
2. **Immediate draw.** Bind
   `RenderContext2::draw_immediate_triangles(data, count)` and
   `draw_immediate_lines(data, count)`, accepting
   `nb::ndarray<float, c_contig>`. C++ already hard-wires the vertex
   layout (`loc 0 = vec3, loc 1 = vec4`).
3. **Texture helpers.** Narrow Python-facing helpers only, not the full
   `IRenderDevice`: `ctx.create_texture_r8(w, h, data)`,
   `ctx.create_texture_rgba8(w, h, data)`,
   `ctx.update_texture_region(handle, x, y, w, h, data)`. Enough to
   support font atlas and `Canvas` partial-update paths.
4. **External GL texture wrapping.** `wrap_gl_texture_as_tgfx2(ctx,
   gl_tex_id, w, h, format)` — for scenarios where a legacy tgfx1
   texture has to be consumed from a tgfx2 pass during the transition.
5. **Scissor.** `ctx.set_scissor(x, y, w, h)` / `ctx.clear_scissor()`.
   Needed for `UIRenderer.begin_clip/end_clip`.
6. **Verify `draw_tc_mesh`.** The binding exists at line 239 of
   `tgfx2_bindings.cpp` but no Python file imports it. Confirm it is
   actually live and callable. If broken, `PlotEngine3D` migration
   becomes harder (would need `create_buffer` / `upload_buffer`
   bindings for meshes).

**Gate:** a Python smoke test creates a `RenderContext2` over an SDL GL
window, clears the default framebuffer via `begin_pass(color={})`, draws
one triangle via `draw_immediate_triangles`, uploads an R8 texture, and
draws a textured quad. All visible on screen.

### Phase 1 — `FontTextureAtlas` tgfx2 upload path *(1 day)*

In `termin-graphics/python/tgfx/font.py`, add a sibling to
`ensure_texture(graphics)`:

```python
def ensure_texture_tgfx2(self, ctx) -> Tgfx2TextureHandle: ...
```

Shared CPU state (the 2048×2048 atlas image, shelf packer, dirty flag)
is unchanged — only the upload call differs. Both methods coexist
through the transition so that phases 2-4 can be tested against tgfx2
without yanking tgfx1 from any still-running consumer.

**Gate:** `ensure_texture_tgfx2(ctx)` returns a handle that
`ctx.bind_sampled_texture(0, handle)` accepts — verified by the Phase 2
test.

### Phase 2 — `Text3DRenderer` on tgfx2 *(0.5 day)*

Rewrite `termin-graphics/python/tgfx/text3d.py`:

- `begin(ctx, camera, aspect, font, mvp_override=None)` — explicit ctx
  parameter.
- Shader compiled via `tc_shader_ensure_tgfx2(ctx, TcShader)`, cached
  per-ctx.
- `ctx.bind_shader(vs, fs)` + `ctx.set_uniform_mat4/vec3/int` +
  `ctx.bind_sampled_texture(0, font.ensure_texture_tgfx2(ctx))`.
- `draw()` accumulates into a Python float list; `end()` dispatches
  `ctx.draw_immediate_triangles(np_array, count)`.

The tgfx1 version is deleted — `PlotEngine3D` (its only consumer) is
migrated in Phase 7.

**Gate:** a standalone smoke test draws "Hello" into an SDL window via
`Text3DRenderer` + a fresh ctx2.

### Phase 3 — `Text2DRenderer` on tgfx2 *(1 day)*

New module `termin-graphics/python/tgfx/text2d.py`. Screen-space text
rendering via orthographic projection, used by `UIRenderer` for
`draw_text` / `draw_text_centered` / `measure_text`.

Essentially a stripped-down `Text3DRenderer`: no `cam_right`/`cam_up`,
an ortho MVP built from viewport dimensions, otherwise the same atlas
and shader.

**Gate:** smoke test renders text at a fixed pixel position, visually
confirmed aligned to pixel grid.

### Phase 4 — `UIRenderer` migration *(3-4 days)*

The core phase. Rewrite the internals of
`termin-gui/python/tcgui/widgets/renderer.py` **without changing the
public API**.

- `__init__(self, graphics, font=None)` — signature unchanged. Stores
  `graphics` for backward compat; internally calls
  `tgfx2.make_context(graphics)` from Phase 0.
- `begin(viewport_w, viewport_h)` — `ctx2.begin_pass(color={})` targets
  default FB, sets viewport, depth test off, blend on. Internal
  batching buffers reset.
- `end()` — flush any pending batches, `ctx2.end_pass()`.
- `draw_rect` / `draw_rect_outline` / `draw_line` — batch into internal
  float buffers, flush on state change (begin_clip/end_clip, end,
  draw_text, draw_image). Draws via
  `ctx2.draw_immediate_triangles/lines`. The UI shader
  (`UI_VERTEX_SHADER`/`UI_FRAGMENT_SHADER`) is compiled via
  `tc_shader_ensure_tgfx2`.
- `draw_text` / `draw_text_centered` / `measure_text` — delegate to
  `Text2DRenderer` from Phase 3.
- `draw_image` — `ctx2.bind_sampled_texture` + a textured-quad shader
  (the second internal shader, already separate today).
- `upload_texture(data)` / `load_image(path)` — via Phase 0 texture
  helpers.
- `begin_clip` / `end_clip` — intersection logic unchanged in Python;
  dispatches to `ctx2.set_scissor` / `clear_scissor`.

**Hidden addition for Phase 7.** Expose `renderer.ctx2` as a property
(read-only). This is the hook `PlotEngine3D` will use later to get the
current frame's ctx2 without reaching around `UIRenderer`.

**Gate:** `diffusion-editor` launches, all layouts correct, text
readable, clicks work, no visual regressions. tcgui layout tests green.
tcplot 2D demos work.

### Phase 5 — `Canvas` and `Viewport3D` migration *(1-2 days)*

- **`canvas.py`** (lines 233-269) — 4 tgfx1 texture calls
  (`create_texture`, `update_texture`, `update_texture_region`, the
  `hasattr` probe). Rewrite against Phase 0 texture helpers. Draw path
  already runs through `UIRenderer`, already tgfx2.
- **`viewport3d.py`** (lines 146-162) — raw GL
  (`glDisable(GL_SCISSOR_TEST)`, `glBindFramebuffer`,
  `glBlitFramebuffer`). This handles blitting a 3D engine's offscreen
  FBO into the UI. On tgfx2 this becomes `ctx2.blit(src_texture,
  dst_texture)`. `RenderContext2::blit` exists in C++ (line 621 of
  `render_context.cpp`); bind it in Phase 0 if not done. Wrapping the
  3D engine's FBO uses `wrap_fbo_color_as_tgfx2` (already bound).

**Gate:** any diffusion-editor Canvas widgets work; any Viewport3D
embedding path works. If there is no live Viewport3D consumer, this
phase shrinks.

### Phase 6 — `diffusion-editor` full validation *(1-2 days)*

Manual walkthrough of diffusion-editor functionality: menus, panels,
dialogs, all widget types (SpinBox, TextInput, Splitter, Canvas, ...),
window resize, themes. Fix regressions as found. No tgfx1 imports
should remain in tcgui. `main.py:8` (`from tgfx import
OpenGLGraphicsBackend`) may stay as a legacy singleton — it is the
graphics backend instance that ultimately feeds `tgfx2.make_context`.
Deciding whether to replace it is orthogonal.

**Gate:** diffusion-editor is fully on tgfx2 in the UI path. Commit as
a milestone.

### Phase 7 — `tcplot` `PlotEngine3D` on tgfx2 *(2 days)*

Rewrite `tcplot/tcplot/engine3d.py`:

- `render(ctx, font)` signature — replaces `render(graphics, font)`.
- Widget adapter `Plot3D.render(renderer)` gets ctx via
  `renderer.ctx2` (property added in Phase 4).
- Shader via `tc_shader_ensure_tgfx2`.
- Meshes via `ctx.draw_tc_mesh(mesh)` (verified live in Phase 0).
- State via `ctx.set_depth_test` / `set_blend` / `set_cull` (bindings
  already exist).
- Marker cross via `ctx.draw_immediate_lines` (bound in Phase 0) or a
  small `TcMesh`.
- Tick labels via `Text3DRenderer`, already on tgfx2 since Phase 2.

**Gate:** `demo_3d_helix.py`, `demo_3d_surface.py` render correctly.

### Phase 8 — `tcplot` `PlotEngine2D` validation *(0.5 day)*

`PlotEngine2D` requires no code changes — it already talks to the duck-
typed draw ctx, which is now `UIRenderer` running on tgfx2.

**Gate:** `demo_sin.py`, `demo_scatter.py`, `demo_multi.py` render
correctly.

## Summary

| # | Phase | Days | Depends on |
|---|---|---|---|
| 0 | tgfx2 Python bindings | 2-3 | — |
| 1 | Font atlas tgfx2 upload | 1 | 0 |
| 2 | Text3D on tgfx2 | 0.5 | 0, 1 |
| 3 | Text2D on tgfx2 | 1 | 0, 1 |
| 4 | UIRenderer migration | 3-4 | 0, 1, 3 |
| 5 | Canvas + Viewport3D | 1-2 | 0, 4 |
| 6 | diffusion-editor validation | 1-2 | 4, 5 |
| 7 | tcplot PlotEngine3D | 2 | 0, 2, 4 |
| 8 | tcplot PlotEngine2D validation | 0.5 | 4 |
| | **Total** | **12-16** | |

## Risks

1. **Visual regression in `UIRenderer`.** The existing
   `draw_ui_vertices` / `draw_ui_textured_quad` path may have sub-pixel
   offsets or blend ordering that shift by 1 px when rewritten through
   `draw_immediate_triangles`. No automated pixel-diff tests exist.
   Mitigation: take a reference screenshot of `diffusion-editor` before
   Phase 4, compare manually after each gate.
2. **`RenderContext2` Python factory — unknown depth.** Phase 0 assumes
   we can cheaply construct a ctx2 over an existing SDL GL context. If
   tgfx2 is architected to own the GL context top-down rather than
   adopt an external one, Phase 0 could balloon from 2-3 days to a
   week. **Mitigation: spend the first day of Phase 0 as a spike.**
   Abandon or re-plan if the spike fails.
3. **`draw_tc_mesh` may not actually work from Python.** The binding is
   in `tgfx2_bindings.cpp:239` but no Python importer found. Verify in
   Phase 0. If dead, Phase 7 needs `create_buffer` / `upload_buffer`
   bindings for mesh data — add them to Phase 0 or accept Phase 7
   scope creep.
4. **`Viewport3D.py` specificity.** Raw GL there is justified (blits a
   3D engine's offscreen buffer into the UI). Porting depends on
   whether `ctx2.blit` + `wrap_fbo_color_as_tgfx2` cover the exact
   geometry transform required. If there is no live consumer of
   `Viewport3D` in diffusion-editor, this risk is zero.
5. **`UI.__init__(graphics)` signature.** diffusion-editor still passes
   a tgfx1 backend. Proposal: keep the parameter, feed it to
   `tgfx2.make_context` internally. That requires zero changes in
   diffusion-editor. Alternative (cleaner signature `UI()`) would
   require one line change upstream.

## Open questions

Before committing to the plan:

1. **Phase 0 spike sign-off.** OK to spend the first day of Phase 0 on a
   spike proving `RenderContext2` can be built over an external GL
   context cheaply? If the spike fails, we re-plan before sinking more
   time.
2. **`UI.__init__` signature.** Keep `(graphics)` parameter for
   backward compat (proposed), or change to `()` and touch
   diffusion-editor?
3. **Visual regression coverage.** Are manual screenshots + eyeballing
   through diffusion-editor and tcplot demos acceptable at each gate?
   Alternative (1-2 days to build a minimal golden-image framework)
   is probably not worth it for this migration's scope.

## Out of scope

- Full tcgui test coverage for rendering. The existing 11 layout tests
  stay; no new draw-correctness tests are added. Coverage comes from
  diffusion-editor manual walkthroughs.
- Removing tgfx1 entirely from Python. After this migration tgfx1 can
  in principle be removed from the Python surface (nothing in tcgui /
  tcplot / diffusion-editor would import it except the
  `OpenGLGraphicsBackend` singleton in `main.py`). Actual removal is a
  follow-up cleanup, not part of this migration.
- C++ `tcplot` port. This plan stops at "Python tcplot runs fully on
  tgfx2". The C++ port and C# bindings happen after this milestone, on
  an engine whose shape has been validated in Python.
