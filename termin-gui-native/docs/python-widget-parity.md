# C++/Python widget parity

This inventory is the maintained public-widget parity contract for
`termin-gui-native`. A concrete C++ widget is public only when Python exposes a
typed wrapper and a `TcDocument.create_*` factory for it. Common lifecycle,
tree, geometry and style operations live on the wrapper's `.widget`
`WidgetRef`; type-specific properties and signals live on the typed wrapper.

`connect_<event>()` returns a connection id. Public signal subscriptions have a
matching `disconnect_<event>(connection)` operation; disconnecting an already
removed connection returns `False`.

| C++ concrete widgets | Python typed wrappers | `TcDocument` factories |
| --- | --- | --- |
| `BoxLayout`, `HStack`, `VStack`, `GridLayout` | same names | `create_box_layout`, `create_hstack`, `create_vstack`, `create_grid_layout` |
| `Panel`, `Label`, `Separator`, `Spacer`, `Swatch` | same names | `create_panel`, `create_label`, `create_separator`, `create_spacer`, `create_swatch` |
| `Button`, `Checkbox`, `Slider`, `SpinBox`, `SliderEdit` | same names | `create_button`, `create_checkbox`, `create_slider`, `create_spin_box`, `create_slider_edit` |
| `TextInput`, `TextArea`, `ComboBox`, `ProgressBar`, `ImageWidget`, `IconButton` | same names | `create_text_input`, `create_text_area`, `create_combo_box`, `create_progress_bar`, `create_image_widget`, `create_icon_button` |
| `GroupBox`, `ScrollArea`, `Splitter`, `TabView`, `OverlayLayout` | same names | `create_group_box`, `create_scroll_area`, `create_splitter`, `create_tab_view`, `create_overlay_layout` |
| `ListWidget`, `FileGridWidget`, `TreeWidget`, `TreeTableWidget`, `TableWidget` | same names | `create_list_widget`, `create_file_grid_widget`, `create_tree_widget`, `create_tree_table_widget`, `create_table_widget` |
| `ToolBar`, `StatusBar`, `Menu`, `MenuBar` | same names | `create_tool_bar`, `create_status_bar`, `create_menu`, `create_menu_bar` |
| `Dialog`, `MessageBox`, `InputDialog`, `FileDialogOverlay`, `ColorPicker`, `ColorDialog` | same names | `create_dialog`, `create_message_box`, `create_input_dialog`, `create_file_dialog`, `create_color_picker`, `create_color_dialog` |
| `Canvas`, `RichTextView`, `FrameTimeGraph`, `FrameTimelineWidget` | `Canvas`, `RichTextView`, `FrameTimeGraph`, `FrameTimelineWidget` | `create_canvas`, `create_rich_text_view`, `create_frame_time_graph`, `create_frame_timeline` |
| `SceneView`, `Viewport3D` | same names | `create_scene_view`, `create_viewport3d` |

`NativeWidget` is intentionally C++-only because it is the implementation base
for native concrete widgets, not a constructible control. `Widget` remains the
Python implementation base used with `adopt()`. Models, document builders,
renderers, graphics scenes and services are supporting types rather than
widgets and are outside this matrix.

When adding a concrete widget, update this table, the package exports, its typed
factory, and installed-SDK lifecycle/signal tests in the same change.
