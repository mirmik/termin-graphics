# termin-gui-native

Experimental native UI document prototype for the future `termin-gui`
migration.

The current module is intentionally small:

- `tc_ui_document` is implemented in C and adopts widget objects while owning
  handle slots and generations;
- `tc_widget` is an intrusive C ABI header embedded into concrete widgets;
- common bounds, size constraints, visibility/enabled/input flags and the
  canonical parent/ordered-children tree live directly in `tc_widget`;
- canonical tree links use stable `tc_widget*` pointers inside a document while
  handles remain the external reference format;
- widget implementations keep their own handle references;
- plain destroy deletes only the requested widget;
- recursive destroy is an explicit API and walks the canonical widget tree;
- the creator-provided deleter may be null for borrowed/static widgets;
- roots are explicit paint entry points, not an implicit ownership tree.
- `termin/gui_native/widgets.hpp` adds a first C++ widget layer:
  `NativeWidget`, `BoxLayout`, `HStack`, `VStack`, `GridLayout`, `GroupBox`,
  `Splitter`, `ScrollArea`, `TabView`, `Panel`, `Button`, `Checkbox`,
  `ProgressBar`, `TextInput`, `Separator`, `Slider`, `Swatch`, `Label`, and
  `Spacer`;
- `tc_ui_document_layout_roots` and `tc_ui_document_dispatch_pointer_event`
  exercise layout and event dispatch without making ownership implicit;
- widget `paint` writes backend-neutral commands into `tc_ui_draw_list` through
  `tc_ui_paint_context`; no GPU renderer is required for unit tests.
- text commands are stored with draw-list-owned string backing and rendered by
  `UiDrawListRenderer` when a default `FontAtlas` is configured;
- `tc_ui_document` accepts a non-owning C text-measure callback with explicit
  UTF-8 byte lengths. `UiDrawListRenderer::bind_text_measurer` adapts its
  `FontAtlas`; the renderer must outlive document layout/paint using that
  binding;
- `Label` and `TextInput` use typographic advances and line metrics. Single-line
  input clips and horizontally scrolls to its caret, while cursor movement and
  deletion preserve UTF-8 codepoint boundaries;
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
  `HStack`, `VStack`, `Panel`, `Label` and `Button` document factories;
- the document-owned Python shim retains its Python body until the C deleter
  runs under the GIL; stale refs remain safe after widget or document teardown;
- `examples/ui_rect_window.py` mirrors the C++ rectangle-window example.

This module does not replace the existing Python `termin-gui` package yet. It
is a place to test the ownership, handle and polyglot widget contracts before
adding Python bindings or porting real widgets.
