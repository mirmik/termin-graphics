# termin-gui-native

Native UI document implementation under active `termin-gui` migration.

Standalone C++ applications can use the installed application host without
depending on `termin-display` or SDL APIs:

```cmake
find_package(termin_gui_native CONFIG REQUIRED)
target_link_libraries(app PRIVATE termin_gui_native::application_host)
```

```cpp
termin::gui_native::Document document;
termin::gui_native::ApplicationHostConfig config;
config.window = {"My utility", 640, 480};
termin::gui_native::ApplicationHost host(document, config);

while (!host.should_close()) {
    update_application_state();
    host.tick();
}
```

The host owns window event routing, text input, the draw list/renderer, the
document clipboard and cursor bridges, the resizable color target, frame
submission and deterministic GPU teardown. Empty font and shader paths resolve
first from `TERMIN_UI_FONT`, `TERMIN_SHADERC`, `TERMIN_SLANGC` and `TERMIN_SDK`,
then relative to the loaded SDK library. All paths remain explicitly
overridable through `ApplicationHostConfig`. Continuous rendering is the
default; event-driven tools can disable it and schedule work with `defer()` and
`request_repaint()`.

The current foundation includes:

- `tc_ui_document` is implemented in C and adopts widget objects while owning
  handle slots and generations;
- the C implementation keeps its single private document state in
  `tc_ui_document_internal.h`. Lifecycle, slot ownership, roots and services
  live in `tc_ui_document.c`; common `tc_widget` state and canonical-tree
  mutation live in `tc_widget.c`; paint composition, overlays, hit testing,
  focus and input routing live in `tc_ui_interaction.c`. These translation
  units share no public state and internal helper symbols remain hidden from
  the shared-library ABI;
- `tc_widget` is an intrusive C ABI header embedded into concrete widgets;
- common bounds, size constraints, visibility/enabled/input flags and the
  canonical parent/ordered-children tree live directly in `tc_widget`;
- stable id, display name and debug name can be set through common C APIs and
  are copied into `tc_widget`-owned storage. C++, Python and restore paths use
  that single storage instead of keeping language-specific identity strings;
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
- owned adoption is atomic: `tc_ui_document_adopt_widget` requires the
  creator-provided deleter in the adoption call;
- borrowed/static widgets use the separate
  `tc_ui_document_attach_borrowed_widget` API and never carry a deleter;
- multilingual widget factories are registered as lifecycle-managed facets in
  the common `tc_runtime_type_registry`, rather than in a parallel UI-only
  registry. Each factory declares its ABI, implementation language, owner and
  parent type, and each result declares owned-with-deleter or borrowed-without-
  deleter semantics explicitly;
- factory-created widgets link their runtime type identity to the common
  instance registry. Unregistering a type first recursively destroys all live
  instances across documents, invalidates their generation handles, and only
  then releases the factory userdata;
- owner hot reload has an explicit invalidation policy through
  `tc_widget_registry_unregister_owner`/`unregister_widget_owner`. All matching
  types are unloaded across documents before replacement factories are
  registered. Recursive topology wins over module ownership: unloading a parent
  also invalidates descendants owned by another module, while their factories
  remain registered and can create fresh generation handles;
- factory ABI v2 carries optional paired serialize/deserialize hooks. Hooks
  exchange owned `tc_value` dictionaries and share the factory lifecycle, so
  module unload cannot leave state callbacks pointing at released userdata;
- `tc_ui_document_capture_snapshot` produces an owned, language-neutral C
  snapshot of widget identity/metadata, slot-order records, canonical child and
  root ordering, geometry, flags, overlays and interaction handles. Copied
  strings and arrays remain valid after the live document changes; the C++
  `DocumentSnapshot` wrapper owns cleanup;
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
- button events carry a host-supplied `click_count` through SDL, the common C
  input manager/event ABI and Python. Collection widgets activate on count 2;
  no widget or editor subsystem owns a timing clock for double-click detection;
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
  text, sampled texture, rounded geometry and nested clip intersection on every
  compiled headless backend (Vulkan on Linux and Vulkan/D3D11 on Windows);
- `build_showcase(Document&)` and `build_python_showcase(Document)` provide
  deterministic C++-built and Python-built native control trees. Their
  headless snapshots fix widget/model state, focus reachability, long UTF-8
  text clipping and draw-command totals without a desktop window;
- the SDL showcase accepts `TERMIN_GUI_NATIVE_SCREENSHOT=/path/to/frame.ppm`.
  Capture mode freezes animated values at their initial state, reads back one
  presented-size frame, writes binary PPM and exits, making the desktop backend
  path suitable for repeatable screenshot comparison;
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
- `CollectionItem` may carry a backend-neutral texture id. `FileGridWidget`
  presents the same shared model as a responsive virtualized tile grid with
  bounded paint, optional icons, multi/range selection, disabled-item keyboard
  navigation, wheel scrolling, pointer-captured scrollbar dragging and
  activation/delete/context-menu signals in both C++ and Python;
- `TreeModel` provides stable node IDs, ordered hierarchy mutation and
  cycle-safe move/reorder operations, while `TreeExpansionModel` owns reusable
  expansion state. `TreeWidget` caches the visible hierarchy projection on
  model revisions, virtualizes paint without per-node widgets, and exposes
  pointer selection/toggle, scrolling, keyboard navigation, activation and
  delete requests consistently in C++ and Python;
- `TableModel` owns stable row IDs and typed structural notifications, while
  `TableColumnModel` owns unique stable column IDs and fixed/stretch sizing
  constraints. `TableWidget` virtualizes row paint, reuses `SelectionModel`,
  exposes header-click and activation signals, and resizes columns through
  document pointer capture with the same API in C++ and Python;
- `CommandModel` is the shared stable-ID source for chrome actions and
  separators, including enabled/checkable/checked, icon, shortcut, tooltip and
  nested-model metadata. `ToolBar` renders and activates that model without
  per-action widgets and uses pointer capture for press/release. `StatusBar`
  exposes deterministic persistent/temporary text; host scheduling explicitly
  decides when to clear temporary messages. `Menu` presents the same model as
  a clamped, bounded-scroll overlay with nested submenu ownership, keyboard
  navigation and outside/Escape dismissal. `MenuBar` switches adjacent menus
  through a single popup owner and dispatches cycle-safe shortcut descriptors;
- `Dialog` provides centered modal composition with canonical content/action
  ownership, nested-modal focus containment/restoration, default/cancel
  keyboard semantics and exactly-once typed results. `MessageBox` and
  `InputDialog` reuse that contract in both C++ and Python; buttons are
  focusable and keyboard-activatable across the native widget set;
- `FileDialogModel` provides deterministic open-file, save-file and
  open-directory semantics over an injected filesystem provider, including
  parsed glob filters, directory-first sorting, history and directory creation.
  `FileDialogOverlay` composes that model on `Dialog` and vetoes invalid accept
  actions without dismissing the modal. `FileDialogService` is the explicit
  host boundary for platform-native pickers; it never selects the overlay as a
  hidden fallback;
- `ColorPickerModel` owns validated HSV/alpha state independently of UI.
  `ColorPicker` provides SV, hue and optional alpha interaction with pointer
  capture, old/new previews and reusable RGBA CPU surfaces. GPU texture IDs and
  upload/update lifetime remain explicitly host-owned; the same widget paints
  renderer-neutral draw-list gradients when no textures are attached.
  `ColorDialog` delivers one optional typed color through the shared modal
  contract, with matching C++ and Python APIs;
- `RichTextModel` owns validated UTF-8 lines and styled segments, with a small
  native HTML-subset adapter for diagnostic content. `RichTextView` wraps via
  the document text service, clips visible rows, provides captured scrollbar
  dragging and read-only source-stable selection/copy through the injected
  clipboard. C++ and Python retain the same shared model; visual wrapping does
  not mutate copied text;
- `FrameTimeModel` is an explicitly host-fed bounded frame-time history, with
  no hidden profiler or clock dependency. `FrameTimeGraph` renders its empty
  state, target/warning guides and right-aligned green/yellow/red bars through
  the native draw list; C++ and Python share the same retained model;
- Python-authored production layouts can select fixed, preferred, flex or
  stretch BoxLayout children and configure padding, spacing, borders and child
  extent limits. The generic append path preserves the C ABI for Python/C
  widget bodies instead of treating them as C++ native widgets;
- `TextInput` changed/submitted signals and table/tree context-menu requests
  are available through the Python bridge. Collection consumers can implement
  live filters and reusable context actions without polling widget internals;
- `Viewport3D` composites an externally owned backend-neutral texture through
  a retained `ViewportSurfaceHost`. Layout performs an ordered
  `before_resize` notification followed by host resize, while pointer, wheel,
  key and UTF-8 text input use typed host methods. The Python bridge accepts
  the same explicit protocol; `termin.display.FBOSurface` implements it with
  typed `tc_input_manager` dispatch and no raw Python pointer transport.
  External drag/drop is a separate typed host callback so OS payload ownership
  never leaks into the core pointer-event ABI;
- `GraphicsScene` and `SceneView` provide the retained 2D tool-scene boundary
  used by node-graph-style editors: exclusive item/child ownership, stable
  z-order, custom draw-list paint and local hit callbacks, selection, captured
  drag/pan and anchored zoom. Embedded native widgets are generation-checked
  canonical document children and detach without implicit destruction. Plot
  annotations intentionally remain owned by `tcplot`;
- `UiDrawListRenderer` can flush the command list through
  `tgfx::Canvas2DRenderer`;
- `TERMIN_GUI_NATIVE_BUILD_EXAMPLES=ON` builds native window examples on the
  public application host; the showcase receives portable pointer, wheel,
  keyboard and text events and owns no SDL/render-target plumbing.
- `TERMIN_BUILD_PYTHON=ON` builds `termin.gui_native`, whose Python-defined
  widgets dispatch the complete measure/layout/paint/input/lifecycle vtable
  through the same embedded `tc_widget` contract;
- Python `WidgetRef` objects contain only an invalidation state plus a handle.
  They expose common state and canonical tree mutation without duplicating
  widget data or retaining the document itself;
- cursor intent is backend-neutral common widget state. `Inherit` walks the
  hovered leaf's ancestors, explicit `Default` stops that walk, and the
  document notifies its host only when the resolved intent changes. SDL cursor
  objects remain owned and cached by the platform host;
- the same `WidgetRef` wraps C++ widgets created by the initial native
  native document factories. Stateful input/media factories return typed
  references without duplicating widget state;
- the document-owned Python shim retains its Python body until the C deleter
  runs under the GIL; stale refs remain safe after widget or document teardown;
- Python widget classes can be registered with `register_widget_type` and
  instantiated by type name through `Document.create_registered_widget`.
  `WidgetRef` exposes the registered type, implementation language and explicit
  ownership policy; constructor failures roll back without leaving a live slot;
- Python `Document.inspect_snapshot()` converts the same C snapshot into plain
  dictionaries, lists, value objects and generation handles. It does not retain
  widgets or expose live native pointers, so editor tooling and MCP diagnostics
  can safely keep a point-in-time result;
- `tc_ui_document_serialize` emits versioned `termin.gui.document` schema v2.
  Records use stable registered type names, common widget state (including
  semantic cursor intent), per-type state
  dictionaries and handle-free record indices for child/root/overlay topology.
  Ephemeral hover, focus, press, capture and dirty flags are intentionally not
  persisted;
- restore is transactional into an empty document: it recreates every record
  through the registered factory, restores common and type state, then attaches
  topology. Any validation, factory or hook failure recursively destroys all
  created widgets. Only registered types are serializable; this makes missing
  type migration explicit instead of silently producing incomplete documents;
- C++ exposes the schema as owning `tc::trent` through `Document::serialize`
  and `Document::restore`. Python exposes detached primitive/list/dict data with
  explicit paired hooks on `register_widget_type`; unsupported Python objects
  are rejected rather than reflected or stringified;
- `examples/ui_rect_window.py` mirrors the C++ rectangle-window example.

This module does not replace the existing Python `termin-gui` package yet. It
is the native implementation under active parity work; ownership, handle,
polyglot widget, input, overlay and theme/style contracts are already exercised
through C, C++ and Python tests.
