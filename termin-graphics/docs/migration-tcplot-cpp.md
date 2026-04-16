# tcplot → C++ migration plan

Companion to `migration-tgfx2.md` and `migration-tgfx2-python.md`.
Goal: port `tcplot/tcplot/*.py` (~1 550 lines) to C++ so it can be
consumed via C# (SWIG) bindings in WPF controls, while keeping the
Python API working via nanobind wrappers.

## Context

`tcplot` is a small plotting library (line / scatter / surface, 2D
and 3D) built on top of `tgfx2`. After the recent `tgfx` → `tgfx2`
migration the engines are already GPU-stack clean:

- `engine3d.py` talks to `tgfx2::RenderContext2` directly via the
  `holder.context` handle — it does not depend on `tcgui`.
- `engine2d.py` takes a duck-typed draw context, currently satisfied
  by `tcgui.UIRenderer`. But a close read shows engine2d only needs
  `draw_rect/line/rect_outline/text`, `measure_text`, and **one** (not
  nested) `begin_clip/end_clip` pair. Everything here is expressible
  in `tgfx2::RenderContext2` + a font atlas + scissor — no `tcgui`
  dependency is required.

So the C++ port targets tgfx2 directly; no C++ clone of `UIRenderer`
is in the critical path.

## Prerequisites that are not yet C++

1. **FontTextureAtlas** — `termin-graphics/python/tgfx/font.py` (313
   lines). Uses PIL for TTF rasterization. Upload path through
   `Tgfx2Context.create_texture/update_texture`. Preloads ASCII
   + a bag of box-drawing / arrow glyphs.
2. **Text3DRenderer** — `termin-graphics/python/tgfx/text3d.py`
   (271 lines). World-space billboard text, uses font atlas, emits
   immediate triangles through the context.
3. **Text2DRenderer** — `termin-graphics/python/tgfx/text2d.py`
   (262 lines). Pixel-space text with ortho projection + Y flip,
   same immediate-triangle path.

These must be C++ before `tcplot::Engine3D` (needs Text3D + atlas)
and `tcplot::Engine2D` (needs Text2D + atlas).

## Ownership / library decisions

- Fonts and text live in `termin_graphics2` (same SHARED lib as the
  rest of tgfx2). Headers: `tgfx2/font_atlas.hpp`, `tgfx2/text2d_renderer.hpp`,
  `tgfx2/text3d_renderer.hpp`. Sources under `src/tgfx2/`.
- TTF rasterization: **stb_truetype**. Single header, already vendored
  at `termin-app/third/recastnavigation/RecastDemo/Contrib/stb_truetype.h`
  — copy to `termin-graphics/third/stb/stb_truetype.h` and make
  `termin_graphics2` own it.
- `tcplot` becomes a C++ module: `tcplot/include/tcplot/`, `tcplot/src/`,
  plus existing `tcplot/tcplot/` Python package becomes thin nanobind
  re-exports (following the "no Python wrappers" rule in
  `termin-app/.claude/CLAUDE.md`).
- TTF file discovery stays out of C++ API. Constructor takes a path
  string. Callers (Python `find_system_font()`, C# app code) pick the
  path.

## Non-obvious zipoints to keep in mind

- **`measure_text` must be pixel-exact with the Python version** —
  tcplot's engine2d right-aligns y-tick labels using it
  (`engine2d.py:260-262`). Mismatch → visible drift.
- **Scissor stack** — tcplot has only one level of clip. Don't need
  a stack in tgfx2. But `FontAtlas` and `Text*Renderer` must not
  leave scissor enabled after their own work.
- **Holder identity reset** — engine3d recompiles shaders when the
  `Tgfx2Context` pointer changes. In C++ this becomes a raw
  `tgfx2::RenderContext2*` comparison; same semantics.
- **Preload glyphs** — font atlas preloads ~140 chars on construction.
  Keep the same set so first-frame pauses match Python.
- **GPU resource ownership across shutdown** — in C# WPF a window
  close happens after GL context teardown. Atlas/shader/mesh
  destructors must tolerate "GL already gone" (no live glDelete calls
  from the finalizer). Today's Python code relies on explicit
  `release_gpu_resources()`; C++ needs the same explicit call and
  defensive destructors.

## Phases

### Phase A — FontTextureAtlas in C++ (prerequisite)

**Deliverables**:

- `tgfx2::FontAtlas` class. Constructor takes TTF path, atlas size
  (default 2048×2048), default pixel size.
- `FontAtlas::ensure_glyph(uint32_t codepoint, float size_px)`
  rasterizes on demand into the CPU atlas, marks dirty.
- `FontAtlas::ensure_texture(RenderContext2*)` creates/updates the
  `tgfx2::TextureHandle` (R8 format) when dirty.
- `FontAtlas::measure_text(string_view, float size_px) → Size2f`
- `FontAtlas::glyph_quad(codepoint, size_px, cursor_x, cursor_y)` →
  returns `{pos_rect, uv_rect, advance_x}` for one glyph.
- Preload same 140 chars as Python.
- Vendored `stb_truetype.h` placed at `termin-graphics/third/stb/`.
  `termin_graphics2` target adds it as include path.
- nanobind binding: `Tgfx2FontAtlas` exposed in `_tgfx_native`;
  `tgfx/font.py` becomes `from tgfx._tgfx_native import FontAtlas
  as FontTextureAtlas` + a tiny constructor helper for
  `find_system_font()`.

**Verification**:

- C++ smoke test: create atlas, ensure "Hello 123", dump atlas PNG,
  compare against Python reference visually.
- Run existing Python `UIRenderer` — text must still render
  identically (byte-for-byte atlas pixel match is NOT the bar;
  visually identical glyphs, same advance widths to within 0.5 px).
- `tcgui` widget tests pass (labels, buttons, tick labels still
  legible, no layout drift).

**Estimated**: 5–7 days.

### Phase B — Text2DRenderer and Text3DRenderer in C++

**Deliverables**:

- `tgfx2::Text2DRenderer`:
  - `begin(RenderContext2*, viewport_w, viewport_h, FontAtlas*)`
  - `draw(string_view, x, y, color, size_px, anchor)`
  - `end()` (restores no state — caller owns pass)
  - Same shader strings as current `text2d.py`.
  - Same CCW winding (TL,BL,BR + TL,BR,TR in pixel y+down).
- `tgfx2::Text3DRenderer`:
  - `begin(RenderContext2*, camera_view, camera_proj, aspect, FontAtlas*)`
  - `draw(string_view, world_pos, color, size)`
  - `end()`
  - Same shader strings as current `text3d.py`.
  - Same CCW winding (BL,BR,TL + BR,TR,TL in NDC y+up).
- Rebind shader + atlas + uniforms on every `draw()` so neighbouring
  passes that flip state (e.g. tcplot's `draw_tc_mesh`) don't leak
  into us. This was the fix for the "white rectangles instead of
  text" bug in the Phase-4 UIRenderer session; preserve it.
- nanobind bindings: `Text2DRenderer`, `Text3DRenderer` in
  `_tgfx_native`; `tgfx/text2d.py` and `tgfx/text3d.py` become
  re-export modules.

**Verification**:

- Visual test: "Hello World" at (100, 100) pixel-space matches prior
  Python render.
- Text3D billboard matches prior Python render for a sample MVP.
- `tcgui` button text still renders correctly.
- `tcplot3d` example: 3D tick labels and marker value label render
  correctly (this exercises Text3D end-to-end).

**Estimated**: 5–7 days (both renderers share a lot of structure).

### Phase C — tcplot C++ core

Scope in one go because the internal modules are tightly interlinked
and small enough not to warrant multiple sub-phases.

**Deliverables**:

- New build target `tcplot` (SHARED), `tcplot/CMakeLists.txt`:
  - `tcplot/include/tcplot/styles.hpp` — color palettes, `cycle_color`,
    `jet(t)`. Trivial.
  - `tcplot/include/tcplot/axes.hpp` — `nice_ticks`, `format_tick`.
    Trivial.
  - `tcplot/include/tcplot/orbit_camera.hpp` — reuses
    `termin::Mat44`, `termin::Vec3`. `view_matrix`, `projection_matrix`,
    `mvp`, `orbit/zoom/pan/fit_bounds`. Numpy matrix math rewritten
    on `Mat44`.
  - `tcplot/include/tcplot/plot_data.hpp` — `LineSeries`,
    `ScatterSeries`, `SurfaceSeries`, `PlotData`.
    Series carry `std::vector<double>` for x/y/z (matches Python
    numpy float64). Surface: two `std::vector<double>` flat + rows/
    cols (Python stores 2D arrays; flattened is trivially
    round-trippable).
  - `tcplot/include/tcplot/engine3d.hpp` + `src/engine3d.cpp` —
    full port. Picking loop becomes a plain for-loop. Uses
    `tgfx2::Text3DRenderer`, `tgfx2::FontAtlas`.
  - `tcplot/include/tcplot/engine2d.hpp` + `src/engine2d.cpp` —
    **not on tcgui**. Talks to `tgfx2::RenderContext2` directly.
    Includes thin immediate-draw helpers
    (`draw_rect`, `draw_line`, `draw_rect_outline`) as private
    methods inside engine2d.cpp (30–50 LOC total).
- `draw_rect` in engine2d: emit 2 CCW triangles (TL,BL,BR +
  TL,BR,TR) via `ctx.draw_immediate_triangles` with pos+color
  layout (same 7-float layout already used by Python immediate path).
- `draw_line`: `ctx.draw_immediate_lines` (thin — no thickness
  support, same as today). Or generate a quad for thickness > 1.
  Match Python behavior exactly.
- `begin_clip/end_clip`: `ctx.set_scissor(x, y, w, h)` and reset.
  One level, no stack.
- Mesh resources: use the C++ `TcMesh` API from `termin-mesh`
  (confirmed available — Python calls it through `tmesh`).

**Verification**:

- Gtests in `tcplot/tests/` for `nice_ticks`, `format_tick`, orbit
  camera transforms (compare numbers to Python reference).
- Visual smoke test app `tcplot/examples/plot3d_smoke.cpp` that
  creates a window (GLFW), calls `Engine3D::scatter`/`surface`,
  renders a few frames. Take a screenshot, compare to the Python
  equivalent.

**Estimated**: 7–10 days.

### Phase D — nanobind wrappers for tcplot

**Deliverables**:

- New `tcplot/python/bindings/tcplot_bindings.cpp`.
- `PlotEngine2D`, `PlotEngine3D`, `PlotData`, `OrbitCamera`,
  `LineSeries`, `ScatterSeries`, `SurfaceSeries` exposed.
- numpy conversion at the boundary: nanobind `ndarray<double,
  nb::c_contig>` → `std::span<const double>` for the add_line /
  add_scatter / surface entry points.
- `tcplot/tcplot/__init__.py` rewritten to `from tcplot._native
  import *`, plus tiny helpers for tcgui widget adapters
  (`plot2d.py`, `plot3d.py` stay as widget adapters — they just
  use the C++ engines).
- `plot2d.py` / `plot3d.py` widget code: updated so
  `engine.render(ctx)` calls the C++ `render(RenderContext2*,
  FontAtlas*)` method, with `ctx` being the `Tgfx2Context`
  underneath `UIRenderer`.

**Verification**:

- Existing Python example scripts (`tcplot/examples/*.py`) run and
  render identically. This is the main go/no-go test; if any script
  breaks, the port is incomplete.
- Specifically: `plot3d` example with interactive orbit + pick +
  marker mode works exactly as before.

**Estimated**: 3–4 days.

### Phase E — C# bindings + WPF demo control

**Deliverables**:

- Extend `termin-csharp/termin.i` with tcplot types, OR create a
  sibling `termin-csharp/tcplot.i` (preferable — keeps tcplot
  bindings loadable independently).
- SWIG wrapping of the C++ API surface:
  `PlotEngine2D`, `PlotEngine3D`, `OrbitCamera`, data-series classes.
  Array passes via `double[]` in/out.
- `termin-csharp/Termin.Native/Plot3DControl.xaml.cs` — WPF
  UserControl analogous to `termin-app/csharp/SceneApp/Controls/
  SceneViewerControl.xaml.cs`: `GLWpfControl`, `tc_opengl_init`,
  hand-made GL context, `PlotEngine3D::render` on each Render tick.
  (Placed alongside `SceneApp` as a demo, not in core csharp lib —
  final packaging is a separate decision.)
- Sample app: line + scatter + surface in a WPF window with mouse
  interaction.

**Verification**:

- Sample app runs, shows expected plot, orbit/pan/zoom work,
  right-click picking prints value to Console.

**Estimated**: 5–7 days.

## Total estimate

**3–4 calendar weeks** of focused work, sequential:
- Phase A: 5–7 days
- Phase B: 5–7 days  (can start once A has a usable atlas)
- Phase C: 7–10 days (needs A + B complete)
- Phase D: 3–4 days  (parallel with Phase E start)
- Phase E: 5–7 days

Optimistic ~18 working days; realistic with bug fix loops ~22–25.

## Risks

1. **Pixel-exact parity of `measure_text`**. PIL and stb_truetype
   compute advance widths slightly differently (hinting, kerning).
   If engine2d's y-tick right-align drifts, fix by baking identical
   metrics conventions — likely need to decide upfront whether we
   match PIL exactly (would require porting PIL's bitmap advance
   rounding) or declare "old was approximate, new is canonical" and
   regenerate Python test references.
2. **Font file path in C#**. Python's `find_system_font()` walks
   `C:\Windows\Fonts\`. In WPF we'd embed the TTF as a resource
   (BuildAction=Embedded), extract to temp on first use, pass path
   to native. Needs a clean answer before Phase E.
3. **stb_truetype vs FreeType quality**. stb_truetype has no
   sub-pixel AA and simpler hinting than FreeType. Text may look
   slightly worse than Python's PIL output. Fallback: switch to
   FreeType later — same atlas/Text renderer API, different
   rasterization back-end. Not on the critical path for shipping
   a working tcplot port.
4. **Surface mesh upload perf at first call**. Python currently
   builds a huge `std::vector` then uploads once; C++ should do the
   same. Beware accidentally using `std::vector::push_back` in a
   tight loop — preallocate.
5. **GC / dispose races in WPF**. Standard WPF hazard; make Plot3D
   state machine explicit about "GL context alive?" before any
   `release_gpu_resources()` call.

## Ordering / dependency graph

```
        [A: FontAtlas C++]
                 |
         +-------+-------+
         |               |
   [B1: Text2D C++]  [B2: Text3D C++]
         |               |
         +-------+-------+
                 |
         [C: tcplot C++ core]
                 |
         +-------+-------+
         |               |
  [D: nanobind wrap] [E: C# SWIG + WPF demo]
         |
    Python regression
    gate: existing
    examples pass.
```

A and B1/B2 can be developed on a single worktree; they share
nanobind binding patterns with `tgfx2_bindings.cpp`.

## Exit criteria

tcplot migration is "done" when:
- All Python `tcplot/examples/*.py` scripts run through C++ engines
  via nanobind wrappers with no visual regression.
- `tgfx/font.py`, `tgfx/text2d.py`, `tgfx/text3d.py` contain only
  re-exports from `_tgfx_native` (no remaining rasterization or
  GPU code in Python).
- A WPF sample app demonstrates scatter + line + surface with
  orbit/pan/zoom/pick.
- Documentation updated: this file marked COMPLETE, pointers added
  to `termin-graphics/docs/` index.
