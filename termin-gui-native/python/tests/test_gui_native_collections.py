import gc
import weakref
from pathlib import Path

import pytest

from termin.gui_native import (
    Color,
    ColorPickerModel,
    ColorPickerSurfaceKind,
    ColorPickerTextureIds,
    CollectionItem,
    CollectionModel,
    CommandData,
    CommandKind,
    CommandModel,
    tc_ui_document_create,
    DialogAction,
    DialogDismissReason,
    DrawCommandType,
    DrawList,
    EventResult,
    FileDialogFilter,
    FileDialogMode,
    FileDialogModel,
    GraphicsItem,
    GraphicsScene,
    KeyCode,
    KeyEvent,
    KeyEventType,
    MessageBoxKind,
    MenuBarEntry,
    SelectionMode,
    ModifierFlag,
    PaintContext,
    Point,
    PointerEvent,
    PointerEventType,
    Rect,
    SceneTransform,
    Size,
    TableColumn,
    TableColumnModel,
    TableColumnPolicy,
    TableModel,
    TableRowData,
    TreeExpansionModel,
    TreeDropPosition,
    TreeModel,
    TreeTableModel,
    TreeTableRowData,
    ViewportExternalDragEvent,
    ViewportExternalDragPhase,
    ViewportSurfaceHost,
)

def _collection_item(index, *, enabled=True, subtitle=""):
    return CollectionItem(f"item-{index}", f"Item {index}", subtitle, enabled)


def test_native_list_widget_model_selection_and_virtualized_paint():
    document = tc_ui_document_create()
    model = CollectionModel()
    model.set_items([_collection_item(index, subtitle="even" if index % 2 == 0 else "odd") for index in range(10_000)])
    revision = model.revision
    widget = document.create_list_widget(model)
    assert widget.model is model
    assert widget.model.item_count == 10_000
    assert widget.model.item(7).stable_id == "item-7"
    assert document.add_root(widget.handle)
    widget.selection_mode = SelectionMode.Multiple
    widget.set_row_height(32.0)
    widget.set_row_spacing(2.0)
    document.layout_roots(Rect(0.0, 0.0, 320.0, 110.0))

    first, last = widget.visible_range
    assert first == 0
    assert last <= 6
    assert widget.content_height > 300_000.0

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Text for command in draw_list.commands) <= 12

    changes = []
    widget.connect_selection_changed(lambda selected: changes.append(list(selected)))
    assert widget.select(2)
    assert widget.select(4, extend=True)
    assert widget.selected_indices == [2, 3, 4]
    assert changes == [[2], [2, 3, 4]]
    model.erase(0)
    assert widget.selected_indices == [1, 2, 3]
    assert changes[-1] == [1, 2, 3]
    widget.ensure_visible(9998)
    assert widget.visible_range[0] > 9990

    model.erase(model.item_count - 1)
    assert model.revision == revision + 2
    document.layout_roots(Rect(0.0, 0.0, 320.0, 110.0))
    assert widget.model.item_count == 9998


def test_native_list_widget_input_callbacks_and_model_lifetime():
    document = tc_ui_document_create()
    model = CollectionModel()
    model.set_items(
        [
            _collection_item(0),
            _collection_item(1),
            _collection_item(2, enabled=False),
            _collection_item(3),
        ]
    )
    widget = document.create_list_widget(model)
    widget.selection_mode = SelectionMode.Multiple
    widget.set_row_height(30.0)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 200.0, 60.0))

    del model
    gc.collect()
    assert widget.model.item_count == 4

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 10.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.selected_indices == [0]

    activated = []
    widget.connect_activated(lambda index, item: activated.append((index, item.stable_id)))
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.selected_indices == [1]
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.selected_indices == [3]
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activated == [(3, "item-3")]

    retained_model = widget.model
    assert document.destroy_widget(widget.handle)
    with pytest.raises(RuntimeError, match="stale"):
        _ = widget.selected_indices
    assert retained_model.item_count == 4


def test_host_click_count_is_exposed_and_drives_list_activation():
    document = tc_ui_document_create()
    model = CollectionModel()
    model.append(CollectionItem("item", "Item"))
    widget = document.create_list_widget(model)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 200.0, 80.0))
    activations = []
    widget.connect_activated(lambda index, item: activations.append((index, item.stable_id)))
    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.button = 0
    pointer.x = 10.0
    pointer.y = 10.0
    pointer.click_count = 1
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert activations == []
    pointer.click_count = 2
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert activations == [(0, "item")]
    pointer.click_count = 3
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert activations == [(0, "item")]


def test_native_list_widget_callback_exceptions_propagate():
    document = tc_ui_document_create()
    model = CollectionModel()
    model.append(_collection_item(0))
    widget = document.create_list_widget(model)

    def fail_selection(_selected):
        raise RuntimeError("list selection failed")

    widget.connect_selection_changed(fail_selection)
    with pytest.raises(RuntimeError, match="list selection failed"):
        widget.select(0)


def test_native_file_grid_widget_virtualizes_responsive_layout_and_textures():
    document = tc_ui_document_create()
    model = CollectionModel()
    model.set_items(
        [
            CollectionItem(
                f"file-{index}",
                f"File {index}",
                ".txt",
                texture_id=77 if index == 0 else 0,
            )
            for index in range(10_000)
        ]
    )
    widget = document.create_file_grid_widget(model)
    widget.set_tile_size(50.0, 60.0)
    widget.set_tile_spacing(4.0)
    widget.set_padding(4.0)
    widget.set_icon_size(20.0)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 220.0, 128.0))

    assert widget.model is model
    assert widget.column_count == 4
    assert widget.row_count == 2500
    assert widget.visible_range[1] <= 16
    assert widget.content_height > 150_000.0
    assert widget.has_scrollbar
    assert widget.scrollbar_thumb_rect.height >= 20.0

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Text for command in draw_list.commands) <= 32
    textures = [command for command in draw_list.commands if command.type == DrawCommandType.Texture]
    assert len(textures) == 1
    assert textures[0].texture_id == 77

    assert widget.select(9999)
    assert widget.scroll_y > 150_000.0
    assert widget.visible_range[0] > 9980
    document.layout_roots(Rect(0.0, 0.0, 112.0, 128.0))
    assert widget.column_count == 2
    assert widget.row_count == 5000


def test_native_file_grid_widget_draws_semantic_icon_without_gpu_texture():
    document = tc_ui_document_create()
    model = CollectionModel()
    model.append(CollectionItem("folder", "Assets", "Folder", icon="folder"))
    widget = document.create_file_grid_widget(model)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 120.0, 100.0))

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert not [command for command in draw_list.commands if command.type == DrawCommandType.Texture]
    assert len([command for command in draw_list.commands if command.type == DrawCommandType.FillRoundedRect]) >= 2


def test_native_file_grid_widget_input_callbacks_lifetime_and_errors():
    document = tc_ui_document_create()
    model = CollectionModel()
    model.set_items(
        [CollectionItem(f"file-{index}", f"File {index}", ".txt", enabled=index != 3) for index in range(20)]
    )
    widget = document.create_file_grid_widget(model)
    widget.set_tile_size(50.0, 30.0)
    widget.set_tile_spacing(0.0)
    widget.set_padding(0.0)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 120.0, 90.0))

    activated = []
    deleted = []
    contexts = []
    drags = []
    widget.connect_activated(lambda index, item: activated.append((index, item.stable_id)))
    widget.connect_delete_requested(lambda index, item: deleted.append((index, item.stable_id)))
    widget.connect_context_menu_requested(lambda index, x, y: contexts.append((index, x, y)))
    widget.connect_drag_requested(
        lambda index, x, y, modifiers: drags.append((index, x, y, modifiers))
    )

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 10.0
    pointer.y = 10.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.current_index == 0
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    pointer.type = PointerEventType.Down
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.current_index == 2
    key.key = KeyCode.Right
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.current_index == 4
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    key.key = KeyCode.Delete
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activated == [(4, "file-4")]
    assert deleted == [(4, "file-4")]

    pointer.button = 1
    pointer.x = 105.0
    pointer.y = 85.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert contexts[-1][0] == -1

    pointer.button = 0
    pointer.type = PointerEventType.Down
    pointer.x = 10.0
    pointer.y = 10.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    pointer.type = PointerEventType.Move
    pointer.x = 160.0
    pointer.y = 140.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert drags == [(0, 160.0, 140.0, 0)]

    pointer.button = 0
    pointer.type = PointerEventType.Down
    pointer.x = 118.0
    pointer.y = 5.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert document.pointer_capture == widget.handle
    pointer.type = PointerEventType.Move
    pointer.y = 50.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.scroll_y > 0.0
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert not document.pointer_capture

    del model
    gc.collect()
    assert widget.model.item_count == 20

    def fail_selection(_selected):
        raise RuntimeError("file grid selection failed")

    widget.connect_selection_changed(fail_selection)
    with pytest.raises(RuntimeError, match="file grid selection failed"):
        widget.select(1)


def test_native_command_model_toolbar_and_status_bar_contracts():
    document = tc_ui_document_create()
    model = CommandModel()
    save = model.append(
        CommandData(
            "save",
            "Save",
            icon="S",
            shortcut="Ctrl+S",
            tooltip="Save scene",
        )
    )
    model.append(CommandData("separator", kind=CommandKind.Separator))
    snap = model.append(CommandData("snap", "Snap", checkable=True))
    model.append(CommandData("disabled", "Disabled", enabled=False))
    toolbar = document.create_tool_bar(model)
    assert toolbar.model is model
    assert document.add_root(toolbar.handle)
    document.layout_roots(Rect(0.0, 0.0, 360.0, 40.0))
    assert model.command_count == 4
    assert len(toolbar.item_rects) == 4
    assert toolbar.item_rects[0].width >= toolbar.item_height
    assert toolbar.item_rects[1].width < toolbar.item_height

    activations = []
    toolbar.connect_activated(
        lambda index, command_id, command: activations.append((index, command_id, command.stable_id, command.checked))
    )
    pointer = PointerEvent()
    pointer.type = PointerEventType.Move
    pointer.x = toolbar.item_rects[0].x + 2.0
    pointer.y = 20.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert toolbar.hovered_tooltip == "Save scene"

    pointer.type = PointerEventType.Down
    pointer.x = toolbar.item_rects[2].x + 2.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert document.pointer_capture == toolbar.handle
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert activations == [(2, snap, "snap", True)]
    assert model.command(snap).data.checked
    model.set_enabled(save, False)
    assert not model.command(save).data.enabled

    status = document.create_status_bar("Ready")
    assert status.displayed_text == "Ready"
    status.show_message("Saved ✓")
    status.text = "Idle"
    assert status.has_message
    assert status.displayed_text == "Saved ✓"
    status.clear_message()
    assert status.displayed_text == "Idle"


def test_native_toolbar_model_lifetime_and_callback_errors():
    document = tc_ui_document_create()
    model = CommandModel()
    model.append(CommandData("run", "Run"))
    toolbar = document.create_tool_bar(model)
    assert document.add_root(toolbar.handle)
    document.layout_roots(Rect(0.0, 0.0, 200.0, 40.0))
    del model
    gc.collect()
    assert toolbar.model.command_count == 1

    def fail_activation(_index, _command_id, _command):
        raise RuntimeError("toolbar activation failed")

    toolbar.connect_activated(fail_activation)
    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = toolbar.item_rects[0].x + 2.0
    pointer.y = 20.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    pointer.type = PointerEventType.Up
    with pytest.raises(RuntimeError, match="toolbar activation failed"):
        document.dispatch_pointer_event(pointer)


def test_native_menu_and_menu_bar_overlay_shortcut_contracts():
    document = tc_ui_document_create()
    submenu = CommandModel()
    submenu.append(CommandData("recent-scene", "Scene.termin"))
    model = CommandModel()
    model.append(CommandData("disabled", "Disabled", enabled=False))
    model.append(CommandData("recent", "Recent", submenu=submenu))
    model.append(CommandData("save", "Save", shortcut="Ctrl+S", checkable=True))
    model.append(CommandData("profiler", "Profiler", shortcut="F7", checkable=True))
    menu = document.create_menu(model)
    menu.max_visible_height = 64.0
    activations = []
    menu.connect_activated(
        lambda index, command_id, command: activations.append((index, command_id, command.stable_id))
    )
    assert menu.show(Point(390.0, 290.0), Rect(0.0, 0.0, 400.0, 300.0))
    assert document.overlay_count == 1
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert menu.current_index == 1
    key.key = KeyCode.Right
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert document.overlay_count == 2
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activations[0][2] == "recent-scene"
    assert document.overlay_count == 0

    bar = document.create_menu_bar()
    bar.entries = [MenuBarEntry("file", "File", model)]
    assert document.add_root(bar.handle)
    document.layout_roots(Rect(0.0, 0.0, 400.0, 30.0))
    bar_activations = []
    bar.connect_activated(
        lambda menu_index, command_id, command: bar_activations.append(
            (menu_index, command_id, command.stable_id, command.checked)
        )
    )
    assert bar.dispatch_shortcut(ord("s"), int(ModifierFlag.Ctrl))
    assert bar_activations[0][2:] == ("save", True)
    assert bar.dispatch_shortcut(KeyCode.F7.value, 0)
    assert bar_activations[1][2:] == ("profiler", True)


def test_native_dialog_message_box_and_input_dialog_contracts():
    document = tc_ui_document_create()
    dialog = document.create_dialog("Confirm")
    dialog.actions = [
        DialogAction("apply", "Apply", is_default=True),
        DialogAction("cancel", "Cancel", is_cancel=True),
    ]
    results = []
    dialog.connect_finished(lambda result: results.append((result.action_id, result.reason)))
    assert dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    assert document.overlay_count == 1
    escape = KeyEvent()
    escape.type = KeyEventType.Down
    escape.key = KeyCode.Escape
    assert document.dispatch_key_event(escape) == EventResult.Handled
    assert results == [("cancel", DialogDismissReason.Escape)]
    assert not dialog.open

    progress_dialog = document.create_dialog("Loading")
    progress_dialog.actions = []
    progress_dialog.dismiss_on_escape = False
    assert progress_dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    assert document.dispatch_key_event(escape) == EventResult.Handled
    assert progress_dialog.open
    assert document.overlay_count == 1
    assert progress_dialog.close()

    message = document.create_message_box("Delete", "Delete selected entity?", MessageBoxKind.Question)
    message_results = []
    message.connect_finished(lambda result: message_results.append(result.action_id))
    assert message.show(Rect(0.0, 0.0, 640.0, 480.0))
    assert document.dispatch_key_event(escape) == EventResult.Handled
    assert message_results == ["no"]

    input_dialog = document.create_input_dialog("Rename", "New name", "Old name")
    values = []
    input_dialog.connect_value_finished(values.append)
    assert input_dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    input_dialog.value = "New name"
    enter = KeyEvent()
    enter.type = KeyEventType.Down
    enter.key = KeyCode.Enter
    assert document.dispatch_key_event(enter) == EventResult.Handled
    assert values == ["New name"]
    assert input_dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    assert document.dispatch_key_event(escape) == EventResult.Handled
    assert values == ["New name", None]


def test_native_file_dialog_model_and_overlay_contract(tmp_path: Path):
    folder = tmp_path / "folder"
    folder.mkdir()
    text_file = tmp_path / "readme.TXT"
    text_file.write_text("hello", encoding="utf-8")
    (tmp_path / "image.png").write_bytes(b"png")

    filters = FileDialogModel.parse_filter_string("Text | *.txt;;Images | *.png")
    assert [(item.label, item.patterns) for item in filters] == [
        ("Text", ["*.txt"]),
        ("Images", ["*.png"]),
    ]
    model = FileDialogModel(FileDialogMode.OpenFile)
    model.set_filters(filters)
    assert model.navigate(str(tmp_path))
    assert [entry.name for entry in model.entries] == ["folder", "readme.TXT"]
    file_index = next(index for index, entry in enumerate(model.entries) if entry.name == "readme.TXT")
    assert model.select(file_index)
    assert model.confirm().path == str(text_file)

    save = FileDialogModel(FileDialogMode.SaveFile)
    assert save.navigate(str(tmp_path))
    save.file_name = "scene.termin"
    assert save.confirm().path == str(tmp_path / "scene.termin")

    document = tc_ui_document_create()
    dialog = document.create_file_dialog(FileDialogMode.OpenFile)
    dialog.set_initial_directory(str(tmp_path))
    dialog.set_filters([FileDialogFilter("Text", ["*.txt"])])
    results = []
    dialog.connect_path_finished(results.append)
    assert dialog.show(Rect(0.0, 0.0, 800.0, 600.0))
    assert not dialog.activate("accept")
    assert dialog.open
    file_index = next(index for index, entry in enumerate(dialog.model.entries) if entry.name == "readme.TXT")
    assert dialog.model.select(file_index)
    assert dialog.activate("accept")
    assert results == [str(text_file)]


def test_native_color_picker_surfaces_and_dialog_contract():
    model = ColorPickerModel(Color(1.0, 0.0, 0.0, 0.5), show_alpha=True)
    assert model.hue == pytest.approx(0.0)
    assert model.saturation == pytest.approx(1.0)
    model.hue = 0.5
    assert model.color.g == pytest.approx(1.0)
    assert model.color.b == pytest.approx(1.0)

    document = tc_ui_document_create()
    picker = document.create_color_picker(model)
    assert picker.model is model
    sv = picker.surface(ColorPickerSurfaceKind.SaturationValue)
    assert (sv.width, sv.height, len(sv.rgba)) == (64, 64, 64 * 64 * 4)
    invalidated = []
    picker.connect_surfaces_invalidated(invalidated.append)
    model.hue = 0.25
    assert invalidated
    assert picker.surface(ColorPickerSurfaceKind.SaturationValue).revision == model.revision

    assert document.add_root(picker.handle)
    document.layout_roots(Rect(0.0, 0.0, 250.0, 244.0))
    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.FillRect for command in draw_list.commands) > 600
    picker.texture_ids = ColorPickerTextureIds(11, 12, 13)
    draw_list.clear()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Texture for command in draw_list.commands) == 3

    dialog = document.create_color_dialog(Color(1.0, 0.0, 0.0, 0.5), show_alpha=True)
    results = []
    dialog.connect_color_finished(results.append)
    assert dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    dialog.color = Color(0.0, 0.5, 1.0, 0.25)
    assert dialog.activate("ok")
    assert results[0].g == pytest.approx(0.5)
    assert results[0].a == pytest.approx(0.25)
    assert dialog.show(Rect(0.0, 0.0, 640.0, 480.0))
    assert dialog.activate("cancel")
    assert results[1] is None


def test_native_tree_model_widget_virtualization_and_navigation():
    document = tc_ui_document_create()
    model = TreeModel()
    expansion = TreeExpansionModel()
    roots = []
    for root_index in range(100):
        root = model.append_root(_collection_item(root_index))
        roots.append(root)
        expansion.set_expanded(root, True)
        for child_index in range(100):
            model.append_child(
                root,
                CollectionItem(
                    f"node-{root_index}-{child_index}",
                    f"Node {root_index}/{child_index}",
                ),
            )
    widget = document.create_tree_widget(model, expansion)
    assert widget.model is model
    assert widget.expansion_model is expansion
    assert document.add_root(widget.handle)
    widget.set_row_height(24.0)
    widget.set_row_spacing(1.0)
    document.layout_roots(Rect(0.0, 0.0, 320.0, 100.0))

    assert model.node_count == 10_100
    assert widget.visible_count == 10_100
    assert widget.visible_range[1] <= 6
    assert widget.visible_row(0).node == roots[0]
    assert widget.visible_row(1).depth == 1
    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Text for command in draw_list.commands) <= 12

    last = model.children(roots[-1])[-1]
    assert widget.select(last)
    assert widget.selected_node == last
    assert widget.visible_range[0] > 10_000

    moved = model.children(roots[0])[0]
    model.move(moved, roots[1])
    assert model.node(moved).parent == roots[1]
    with pytest.raises(ValueError, match="cycle"):
        model.move(roots[1], moved)


def test_native_tree_widget_pointer_callbacks_reconcile_and_propagate_errors():
    document = tc_ui_document_create()
    model = TreeModel()
    root = model.append_root(CollectionItem("root", "Root"))
    first = model.append_child(root, CollectionItem("first", "First"))
    disabled = model.append_child(root, CollectionItem("disabled", "Disabled", enabled=False))
    last = model.append_child(root, CollectionItem("last", "Last"))
    widget = document.create_tree_widget(model)
    assert document.add_root(widget.handle)
    widget.set_row_height(30.0)
    document.layout_roots(Rect(0.0, 0.0, 220.0, 90.0))

    expansions = []
    selections = []
    activated = []
    deleted = []
    contexts = []
    widget.connect_expansion_changed(lambda node, value: expansions.append((node, value)))
    widget.connect_selection_changed(lambda node: selections.append(node))
    widget.connect_activated(lambda node, item: activated.append((node, item.stable_id)))
    widget.connect_delete_requested(lambda node, item: deleted.append((node, item.stable_id)))
    widget.connect_context_menu_requested(lambda node, x, y: contexts.append((node, x, y)))

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 5.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert expansions == [(root, True)]

    pointer.x = 40.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.selected_node == root
    pointer.button = 1
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert contexts == [(root, 40.0, 15.0)]
    pointer.button = 0
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.selected_node == first
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.selected_node == last
    assert widget.selected_node != disabled
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    key.key = KeyCode.Delete
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activated == [(last, "last")]
    assert deleted == [(last, "last")]

    model.erase(last)
    assert widget.selected_node == 0
    assert selections[-1] == 0

    def fail_selection(_node):
        raise RuntimeError("tree selection failed")

    widget.connect_selection_changed(fail_selection)
    with pytest.raises(RuntimeError, match="tree selection failed"):
        widget.select(first)


def test_native_tree_widget_drag_drop_signal_reports_position():
    document = tc_ui_document_create()
    model = TreeModel()
    first = model.append_root(CollectionItem("first", "First"))
    second = model.append_root(CollectionItem("second", "Second"))
    widget = document.create_tree_widget(model)
    widget.draggable = True
    assert document.add_root(widget.handle)
    widget.set_row_height(30.0)
    document.layout_roots(Rect(0.0, 0.0, 220.0, 90.0))
    drops = []
    widget.connect_drop_requested(lambda dragged, target, position: drops.append((dragged, target, position)))

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.button = 0
    pointer.x = 40.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    pointer.type = PointerEventType.Move
    pointer.y = 45.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.dragging
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert drops == [(first, second, TreeDropPosition.Inside)]


def test_native_tree_table_preserves_identity_expansion_and_columns():
    model = TreeTableModel()
    model.set_rows(
        [
            TreeTableRowData("root", "", ["Root", "12.0"]),
            TreeTableRowData("child/with/slash", "root", ["Child/With/Slash", "8.0"]),
            TreeTableRowData("other", "", ["Other", "2.0"]),
        ]
    )
    root = model.find("root")
    child = model.find("child/with/slash")
    assert model.node(root).children == [child]

    columns = TableColumnModel()
    columns.set_columns(
        [
            TableColumn("section", "Section", min_width=120.0),
            TableColumn("ms", "ms", TableColumnPolicy.Fixed, width=60.0),
        ]
    )
    expansion = TreeExpansionModel()
    expansion.set_expanded(root, True)
    document = tc_ui_document_create()
    widget = document.create_tree_table_widget(model, columns, expansion)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 320.0, 150.0))
    assert widget.visible_count == 3
    assert widget.visible_row(1).depth == 1
    assert widget.column_layout[1].width == pytest.approx(60.0)

    assert widget.select(child)
    model.set_rows(
        [
            TreeTableRowData("root", "", ["Root", "10.0"]),
            TreeTableRowData("child/with/slash", "root", ["Child/With/Slash", "7.0"]),
            TreeTableRowData("other", "", ["Other", "3.0"]),
        ]
    )
    assert model.find("root") == root
    assert model.find("child/with/slash") == child
    assert widget.selected_node == child
    assert widget.expanded(root)
    widget.set_expanded(root, False)
    assert widget.visible_count == 2


def test_native_table_widget_models_layout_and_virtualized_paint():
    document = tc_ui_document_create()
    model = TableModel()
    model.set_rows([TableRowData(f"row-{index}", [f"Row {index}", str(index), "Ready"]) for index in range(10_000)])
    columns = TableColumnModel()
    columns.set_columns(
        [
            TableColumn(
                "name",
                "Name",
                TableColumnPolicy.Fixed,
                width=80.0,
                min_width=60.0,
            ),
            TableColumn("value", "Value", min_width=40.0),
            TableColumn("state", "State", min_width=40.0, max_width=160.0, stretch=2.0),
        ]
    )
    widget = document.create_table_widget(model, columns)
    assert widget.model is model
    assert widget.column_model is columns
    assert document.add_root(widget.handle)
    widget.set_row_height(24.0)
    widget.set_header_height(28.0)
    document.layout_roots(Rect(0.0, 0.0, 400.0, 128.0))

    assert model.row_count == 10_000
    assert columns.column_count == 3
    assert widget.visible_range[1] <= 6
    assert widget.content_height == pytest.approx(240_000.0)
    assert widget.column_layout[0].width == pytest.approx(80.0)
    assert widget.column_layout[2].width == pytest.approx(160.0)
    assert widget.column_layout[2].x + widget.column_layout[2].width == pytest.approx(400.0)

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert sum(command.type == DrawCommandType.Text for command in draw_list.commands) <= 21

    changes = []
    widget.selection_mode = SelectionMode.Multiple
    widget.connect_selection_changed(lambda selected: changes.append(list(selected)))
    assert widget.select(2)
    assert widget.select(4, extend=True)
    assert widget.selected_indices == [2, 3, 4]
    first_id = model.row_at(0).id
    model.erase(first_id)
    assert widget.selected_indices == [1, 2, 3]
    assert changes[-1] == [1, 2, 3]
    widget.ensure_visible(9998)
    assert widget.visible_range[0] > 9990


def test_native_table_widget_input_resize_callbacks_lifetime_and_errors():
    document = tc_ui_document_create()
    model = TableModel()
    first = model.append(TableRowData("first", ["First", "1"]))
    model.append(TableRowData("disabled", ["Disabled", "2"], enabled=False))
    last = model.append(TableRowData("last", ["Last", "3"]))
    columns = TableColumnModel()
    columns.set_columns(
        [
            TableColumn(
                "name",
                "Name",
                TableColumnPolicy.Fixed,
                width=100.0,
                min_width=60.0,
                max_width=180.0,
            ),
            TableColumn("value", "Value"),
        ]
    )
    widget = document.create_table_widget(model, columns)
    widget.set_row_height(30.0)
    widget.set_header_height(30.0)
    assert document.add_root(widget.handle)
    document.layout_roots(Rect(0.0, 0.0, 260.0, 120.0))

    headers = []
    resized = []
    activated = []
    contexts = []
    widget.connect_header_clicked(lambda index, column: headers.append((index, column.stable_id)))
    widget.connect_column_resized(lambda index, width: resized.append((index, width)))
    widget.connect_activated(lambda index, row, data: activated.append((index, row, data.stable_id)))
    widget.connect_context_menu_requested(lambda index, x, y: contexts.append((index, x, y)))

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 20.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert headers == [(0, "name")]

    pointer.y = 45.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert widget.current_index == 0
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Down
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert widget.current_index == 2
    key.key = KeyCode.Enter
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert activated == [(2, last, "last")]

    pointer.button = 1
    pointer.x = 20.0
    pointer.y = 45.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert contexts == [(0, 20.0, 45.0)]
    pointer.button = 0

    pointer.type = PointerEventType.Down
    pointer.x = 100.0
    pointer.y = 15.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert document.pointer_capture == widget.handle
    pointer.type = PointerEventType.Move
    pointer.x = 140.0
    pointer.y = -50.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert resized[-1] == pytest.approx((0, 140.0))
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert not document.pointer_capture

    model.insert(0, TableRowData("inserted", ["Inserted", "0"]))
    assert model.index_of(first) == 1
    del model
    del columns
    gc.collect()
    assert widget.model.row_count == 4
    assert widget.column_model.column_count == 2

    def fail_selection(_selected):
        raise RuntimeError("table selection failed")

    widget.connect_selection_changed(fail_selection)
    with pytest.raises(RuntimeError, match="table selection failed"):
        widget.select(0)


def test_native_viewport3d_surface_protocol_input_drag_and_lifetime():
    class SurfaceHost:
        def __init__(self):
            self.valid = True
            self.size = (64, 64)
            self.texture_id = 91
            self.calls = []

        def is_valid(self):
            return self.valid

        def get_tgfx_color_tex_id(self):
            return self.texture_id

        def framebuffer_size(self):
            return self.size

        def resize(self, width, height):
            self.calls.append(("resize", width, height))
            self.size = (width, height)
            return True

        def dispatch_pointer_move(self, x, y):
            self.calls.append(("move", x, y))
            return True

        def dispatch_pointer_button(self, x, y, button, action, modifiers, click_count):
            self.calls.append(("button", x, y, button, action, modifiers, click_count))
            return True

        def dispatch_wheel(self, x, y, wheel_x, wheel_y, modifiers):
            self.calls.append(("wheel", x, y, wheel_x, wheel_y, modifiers))
            return True

        def dispatch_key(self, key, scancode, action, modifiers):
            self.calls.append(("key", key, scancode, action, modifiers))
            return True

        def dispatch_text(self, codepoint):
            self.calls.append(("text", codepoint))
            return True

    document = tc_ui_document_create()
    viewport = document.create_viewport3d()
    assert document.add_root(viewport.handle)
    surface = SurfaceHost()
    assert isinstance(surface, ViewportSurfaceHost)
    weak_surface = weakref.ref(surface)
    ordering = []
    viewport.connect_before_resize(
        lambda previous, next_size: ordering.append(
            (previous.width, previous.height, next_size.width, next_size.height)
        )
    )
    document.layout_roots(Rect(10.2, 20.3, 300.8, 180.9))
    viewport.set_surface_host(surface)
    assert ordering == [(64, 64, 301, 181)]
    assert surface.calls == [("resize", 301, 181)]
    assert viewport.has_surface
    assert viewport.surface_valid
    assert viewport.surface_size.width == 301

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    texture_commands = [command for command in draw_list.commands if command.type == DrawCommandType.Texture]
    assert len(texture_commands) == 1
    assert texture_commands[0].texture_id == 91
    destination = texture_commands[0].rect
    assert (destination.x, destination.y) == (10.0, 20.0)
    assert (destination.width, destination.height) == (301.0, 181.0)

    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.x = 42.0
    pointer.y = 65.0
    pointer.button = 1
    pointer.click_count = 2
    pointer.modifiers = 7
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert surface.calls[-2:] == [
        ("move", 32.0, 45.0),
        ("button", 32.0, 45.0, 1, 1, 7, 2),
    ]

    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.A
    key.scancode = 9
    key.modifiers = 3
    key.repeat = True
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert surface.calls[-1] == ("key", 65, 9, 2, 3)
    assert document.dispatch_text_event("AЖ") == EventResult.Handled
    assert surface.calls[-2:] == [("text", ord("A")), ("text", ord("Ж"))]

    drops = []
    viewport.set_external_drag_handler(lambda event: drops.append(event.payload) or True)
    drag = ViewportExternalDragEvent(
        ViewportExternalDragPhase.Drop,
        "text/uri-list",
        "file:///tmp/scene.tscene",
        12.0,
        14.0,
    )
    assert viewport.dispatch_external_drag(drag)
    assert drops == ["file:///tmp/scene.tscene"]

    del surface
    gc.collect()
    assert weak_surface() is not None
    viewport.detach_surface()
    gc.collect()
    assert weak_surface() is None
    assert not viewport.has_surface


def test_native_viewport3d_stale_surface_and_destroy_release_are_safe():
    class StaleSurface:
        def __init__(self):
            self.valid = False

        def is_valid(self):
            return self.valid

        def get_tgfx_color_tex_id(self):
            raise AssertionError("stale texture must not be queried")

        def framebuffer_size(self):
            raise AssertionError("stale size must not be queried")

        def resize(self, width, height):
            raise AssertionError("stale surface must not resize")

        def dispatch_pointer_move(self, x, y):
            raise AssertionError("stale surface must not receive input")

        def dispatch_pointer_button(self, x, y, button, action, modifiers, click_count):
            raise AssertionError("stale surface must not receive input")

        def dispatch_wheel(self, x, y, wheel_x, wheel_y, modifiers):
            raise AssertionError("stale surface must not receive input")

        def dispatch_key(self, key, scancode, action, modifiers):
            raise AssertionError("stale surface must not receive input")

        def dispatch_text(self, codepoint):
            raise AssertionError("stale surface must not receive input")

    document = tc_ui_document_create()
    viewport = document.create_viewport3d()
    assert document.add_root(viewport.handle)
    surface = StaleSurface()
    weak_surface = weakref.ref(surface)
    viewport.set_surface_host(surface)
    del surface
    gc.collect()
    assert weak_surface() is not None
    assert not viewport.surface_valid
    assert viewport.texture_id == 0
    document.layout_roots(Rect(0.0, 0.0, 200.0, 100.0))
    assert document.destroy_widget(viewport.handle)
    gc.collect()
    assert weak_surface() is None


def test_display_is_the_viewport_surface_and_input_protocol():
    from termin.display import Display

    surface_methods = (
        "is_valid",
        "get_tgfx_color_tex_id",
        "framebuffer_size",
        "resize",
    )
    input_methods = (
        "dispatch_pointer_move",
        "dispatch_pointer_button",
        "dispatch_wheel",
        "dispatch_key",
        "dispatch_text",
    )
    for method_name in surface_methods:
        assert callable(getattr(Display, method_name))
    for method_name in input_methods:
        assert callable(getattr(Display, method_name))


def test_native_scene_view_model_transform_drag_callbacks_and_embedding():
    scene = GraphicsScene()
    node = GraphicsItem("node-a")
    node.position = Point(10.0, 20.0)
    node.size = Size(120.0, 70.0)
    node.draggable = True
    painted = []

    def paint_item(item, context, transform):
        painted.append((item.stable_id, transform.zoom))
        screen = transform.world_to_screen(item.world_position)
        context.fill_rect(
            Rect(screen.x, screen.y, item.size.width * transform.zoom, item.size.height * transform.zoom),
            Color(0.8, 0.2, 0.1, 1.0),
        )

    node.set_paint_callback(paint_item)
    assert scene.add_item(node)
    assert scene.hit_test(20.0, 30.0) is node

    edge = GraphicsItem("edge")
    edge.selectable = False
    edge.z_index = -10.0
    edge.set_hit_test_callback(lambda _item, x, y: abs(y) < 5.0 and 0.0 <= x <= 200.0)
    assert scene.add_item(edge)
    assert scene.hit_test(50.0, 2.0) is edge

    document = tc_ui_document_create()
    view = document.create_scene_view(scene)
    assert document.add_root(view.handle)
    document.layout_roots(Rect(100.0, 50.0, 400.0, 300.0))
    assert view.world_to_screen(Point(10.0, 20.0)).x == pytest.approx(110.0)

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    assert painted == [("node-a", 1.0)]
    assert any(command.type == DrawCommandType.FillRect for command in draw_list.commands)

    moved = []
    view.connect_item_moved(lambda item: moved.append(item.stable_id))
    pointer = PointerEvent()
    pointer.type = PointerEventType.Down
    pointer.button = 0
    pointer.x = 120.0
    pointer.y = 80.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert scene.selected_items == [node]
    pointer.type = PointerEventType.Move
    pointer.x = 150.0
    pointer.y = 110.0
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    assert node.position.x == pytest.approx(40.0)
    assert node.position.y == pytest.approx(50.0)
    assert moved == ["node-a"]
    pointer.type = PointerEventType.Up
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled

    anchor = Point(250.0, 160.0)
    before = view.screen_to_world(anchor)
    pointer.type = PointerEventType.Wheel
    pointer.wheel_y = 1.0
    pointer.x = anchor.x
    pointer.y = anchor.y
    assert document.dispatch_pointer_event(pointer) == EventResult.Handled
    after = view.screen_to_world(anchor)
    assert view.zoom > 1.0
    assert after.x == pytest.approx(before.x)
    assert after.y == pytest.approx(before.y)

    embedded = document.create_button("Embedded")
    view.set_zoom(1.0, Point(100.0, 50.0))
    view.offset = Point(0.0, 0.0)
    editor_item = GraphicsItem("editor")
    editor_item.position = Point(5.0, 6.0)
    editor_item.size = Size(100.0, 30.0)
    editor_item.embedded_widget = embedded.handle
    assert scene.add_item(editor_item)
    document.layout_roots(Rect(100.0, 50.0, 400.0, 300.0))
    screen = view.world_to_screen(editor_item.position)
    assert document.hit_test(screen.x + 2.0, screen.y + 2.0) == embedded.handle
    assert scene.remove_item(editor_item)
    document.layout_roots(Rect(100.0, 50.0, 400.0, 300.0))
    assert embedded.widget.parent is None
    view.set_pointer_handler(None)
    view.set_key_handler(None)
    view.set_text_handler(None)


def test_native_scene_transform_and_scene_view_handler_errors_propagate():
    transform = SceneTransform(10.0, 20.0, 2.0)
    screen = transform.world_to_screen(Point(3.0, 4.0))
    assert (screen.x, screen.y) == pytest.approx((16.0, 28.0))
    world = transform.screen_to_world(screen)
    assert (world.x, world.y) == pytest.approx((3.0, 4.0))

    document = tc_ui_document_create()
    view = document.create_scene_view()
    assert document.add_root(view.handle)
    document.layout_roots(Rect(0.0, 0.0, 100.0, 100.0))

    def fail_pointer(_world, _event):
        raise RuntimeError("scene pointer failed")

    view.set_pointer_handler(fail_pointer)
    event = PointerEvent()
    event.type = PointerEventType.Down
    event.x = 10.0
    event.y = 10.0
    with pytest.raises(RuntimeError, match="scene pointer failed"):
        document.dispatch_pointer_event(event)
