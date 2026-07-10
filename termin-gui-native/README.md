# termin-gui-native

Native UI document implementation under active `termin-gui` migration.

The current foundation includes:

- `tc_ui_document` is implemented in C and adopts widget objects while owning
  handle slots and generations;
- `tc_widget` is an intrusive C ABI header embedded into concrete widgets;
- common bounds, size constraints, visibility/enabled/input flags and the
  canonical parent/ordered-children tree live directly in `tc_widget`;
- each `tc_widget` stores a semantic style role and one masked override by
  value. An override may opt into canonical-tree inheritance; there is no
  parallel style record or language-specific style cache;
- `tc_ui_document` owns its `tc_ui_theme` by value and exposes a monotonic
  revision. Replacing the theme invalidates layout, paint and state for every
  live widget;
- computed styles resolve role base, interaction layers, inherited ancestor
  overrides and the local override in that order. Hover, press, focus and
  effective disabled state come from the document; checked state is supplied
  by widgets such as `Checkbox`;
- canonical tree links use stable `tc_widget*` pointers inside a document while
  handles remain the external reference format;
- widget implementations keep their own handle references;
- plain destroy deletes only the requested widget;
- recursive destroy is an explicit API and walks the canonical widget tree;
- the creator-provided deleter may be null for borrowed/static widgets;
- roots are explicit paint entry points, not an implicit ownership tree.
- the C++ widget layer keeps one public header per concrete class and, where
  behavior is not inline, a matching implementation file
  (`button.hpp`/`button.cpp`, `grid_layout.hpp`/`grid_layout.cpp`, and so on).
  `termin/gui_native/widgets.hpp` is the compatibility umbrella for consumers
  that want the complete widget set;
- `tc_ui_document_layout_roots` and the input dispatch APIs exercise layout
  and event routing without making ownership implicit;
- pointer, key and text input route from target to root over a snapshot of
  generation handles. A callback may destroy or reparent route members; stale
  handles are skipped without dereferencing freed language bodies;
- hover changes emit direct `Enter`/`Leave` notifications, while ordinary
  pointer events bubble until `Handled`. Pointer down records the handling
  widget as pressed, and capture overrides pressed routing until release;
- focus changes emit an explicit vtable callback. Unhandled `Tab` and
  `Shift+Tab` cycle over effectively visible/enabled focusable widgets in
  canonical depth-first order;
- hiding or disabling a subtree clears hover, pressed, capture and focus state
  that points into it through the common `tc_widget` setters;
- ordered overlays are non-owning presentation entries over the same canonical
  widgets. They paint after roots and win hit testing from top to bottom;
- overlay flags provide modal barriers, outside-click dismissal and
  pointer-transparent tooltip behavior. Dismissal has an explicit reason and
  crosses the C++/Python vtable like other lifecycle notifications;
- tooltip delay/scheduling belongs to the host. `tc_ui_tooltip_rect` is a pure
  helper for offset placement and viewport clamping;
- widget `paint` writes backend-neutral commands into `tc_ui_draw_list` through
  `tc_ui_paint_context`; commands cover rect/rounded rect, circle/arc,
  line/polyline, texture, text and nested clips. No GPU renderer is required
  for command unit tests;
- text and polyline commands use draw-list-owned backing storage. Texture
  commands carry a non-owning backend-neutral texture id whose device resource
  must remain alive through `UiDrawListRenderer::render`;
- `UiDrawListRenderer` renders every command through `Canvas2DRenderer` when a
  default `FontAtlas` is configured for text. Its offscreen pixel smoke covers
  text, sampled texture, rounded geometry and nested clip intersection;
- `tc_ui_document` accepts a non-owning C text-measure callback with explicit
  UTF-8 byte lengths. `UiDrawListRenderer::bind_text_measurer` adapts its
  `FontAtlas`; the renderer must outlive document layout/paint using that
  binding;
- `Label`, `TextInput` and `TextArea` use typographic advances and line metrics.
  Both editors expose byte-offset carets normalized to UTF-8 codepoint
  boundaries, selection, mouse/keyboard navigation and copy/cut/paste through
  document-level host clipboard callbacks. `TextInput` scrolls horizontally;
  the unwrapped multiline `TextArea` scrolls on both axes;
- `Slider` supports arbitrary finite ranges and optional step quantization.
  `SliderEdit` materializes a canonical `Slider` + `SpinBox` child pair on its
  first document layout and keeps programmatic and interactive values in sync;
- `ComboBox` uses one document-owned, reusable overlay widget with outside
  dismissal, keyboard selection and wheel scrolling. The first native pass
  omits the legacy draggable scrollbar thumb while preserving selection and
  scroll behavior;
- `IconButton` accepts either UTF-8 icon text or a texture id. `ImageWidget`
  and `Canvas` accept non-owning tgfx2 texture ids and explicit image sizes;
  file decoding, upload, update and destruction remain host responsibilities.
  `Canvas` provides fit/zoom/pan transforms plus a backend-neutral custom paint
  callback inside its clip;
- `CollectionModel` owns flat item data and emits typed reset/insert/update/erase
  changes. `SelectionModel` provides reusable single, multiple and anchored
  range selection while preserving indices across structural insert/erase;
- `ListWidget` retains a shared collection model, paints only its computed
  visible row range, scrolls without materializing per-row widgets, skips
  disabled rows during direct/keyboard navigation and exposes the same model,
  selection, activation and lifetime contract to C++ and Python;
- `UiDrawListRenderer` can flush the command list through
  `tgfx::Canvas2DRenderer`;
- `TERMIN_GUI_NATIVE_BUILD_EXAMPLES=ON` builds a small SDL window example that
  presents a few colored rectangles and a line.
- `TERMIN_BUILD_PYTHON=ON` builds `termin.gui_native`, whose Python-defined
  widgets dispatch the complete measure/layout/paint/input/lifecycle vtable
  through the same embedded `tc_widget` contract;
- Python `WidgetRef` objects contain only an invalidation state plus a handle.
  They expose common state and canonical tree mutation without duplicating
  widget data or retaining the document itself;
- the same `WidgetRef` wraps C++ widgets created by the initial native
  native document factories. Stateful input/media factories return typed
  references without duplicating widget state;
- the document-owned Python shim retains its Python body until the C deleter
  runs under the GIL; stale refs remain safe after widget or document teardown;
- `examples/ui_rect_window.py` mirrors the C++ rectangle-window example.

This module does not replace the existing Python `termin-gui` package yet. It
is the native implementation under active parity work; ownership, handle,
polyglot widget, input, overlay and theme/style contracts are already exercised
through C, C++ and Python tests.
