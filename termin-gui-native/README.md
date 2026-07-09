# termin-gui-native

Experimental native UI document prototype for the future `termin-gui`
migration.

The current module is intentionally small:

- `tc_ui_document` owns widget objects and handle lifetime;
- `tc_widget` is an intrusive C ABI header embedded into concrete widgets;
- widget implementations keep their own handle references;
- plain destroy deletes only the requested widget;
- recursive destroy is an explicit API and asks the widget vtable for recursive
  destroy targets;
- roots are explicit paint entry points, not an implicit ownership tree.
- `termin/gui_native/widgets.hpp` adds a first C++ widget layer:
  `NativeWidget`, `BoxLayout`, `Panel`, `Button`, `Checkbox`, `ProgressBar`,
  `Slider`, `Swatch`, `Label`, and `Spacer`;
- `tc_ui_document_layout_roots` and `tc_ui_document_dispatch_pointer_event`
  exercise layout and event dispatch without making ownership implicit;
- widget `paint` writes backend-neutral commands into `tc_ui_draw_list` through
  `tc_ui_paint_context`; no GPU renderer is required for unit tests.
- text commands are stored with draw-list-owned string backing and rendered by
  `UiDrawListRenderer` when a default `FontAtlas` is configured;
- `UiDrawListRenderer` can flush the command list through
  `tgfx::Canvas2DRenderer`;
- `TERMIN_GUI_NATIVE_BUILD_EXAMPLES=ON` builds a small SDL window example that
  presents a few colored rectangles and a line.
- `TERMIN_BUILD_PYTHON=ON` builds `termin.gui_native`, a nanobind bridge for
  Python widgets that emit the same draw-list commands; `examples/ui_rect_window.py`
  mirrors the C++ rectangle-window example.

This module does not replace the existing Python `termin-gui` package yet. It
is a place to test the ownership, handle and polyglot widget contracts before
adding Python bindings or porting real widgets.
