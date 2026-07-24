"""Deterministic Python-built native UI showcase used by examples and QA gates."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from ._gui_native import (
    CollectionItem,
    CollectionModel,
    CommandData,
    CommandKind,
    CommandModel,
    TcDocument,
    FrameTimeModel,
    Size,
    TableColumn,
    TableColumnModel,
    TableColumnPolicy,
    TableModel,
    TableRowData,
    TreeExpansionModel,
    TreeModel,
    WidgetRef,
)


@dataclass(frozen=True)
class PythonShowcase:
    """Strong references to the representative controls and their shared models."""

    root: WidgetRef
    widgets: dict[str, Any]
    models: tuple[Any, ...]


def _widget(document: TcDocument, reference: Any) -> WidgetRef:
    if isinstance(reference, WidgetRef):
        return reference
    return document.ref(reference.handle)


def _append(
    document: TcDocument,
    parent: WidgetRef,
    reference: Any,
    *,
    preferred: Size | None = None,
) -> WidgetRef:
    child = _widget(document, reference)
    if preferred is not None:
        child.preferred_size = preferred
    if not parent.append_child(child):
        raise RuntimeError("failed to append Python showcase widget")
    return child


def build_python_showcase(document: TcDocument) -> PythonShowcase:
    """Build a real native-widget tree from Python without compatibility widgets."""

    root = document.create_vstack("python-showcase-root")
    root.stable_id = "python-showcase.root"
    root.preferred_size = Size(800.0, 600.0)
    if not document.add_root(root.handle):
        raise RuntimeError("failed to add Python showcase root")

    command_model = CommandModel()
    command_model.set_commands(
        [
            CommandData("save", "Save", icon="S", shortcut="Ctrl+S"),
            CommandData("separator", kind=CommandKind.Separator),
            CommandData("snap", "Snap", checkable=True, checked=True),
        ]
    )
    toolbar = document.create_tool_bar(command_model)
    _append(document, root, toolbar, preferred=Size(800.0, 40.0))

    body = document.create_hstack("python-showcase-body")
    _append(document, root, body, preferred=Size(800.0, 536.0))

    navigation = document.create_vstack("python-showcase-navigation")
    _append(document, body, navigation, preferred=Size(180.0, 536.0))
    _append(document, navigation, document.create_label("Native UI from Python"))
    _append(document, navigation, document.create_button("Scene"))
    _append(document, navigation, document.create_button("Assets"))
    text_input = document.create_text_input("Scene 01 — UTF-8")
    _append(document, navigation, text_input, preferred=Size(180.0, 34.0))

    content = document.create_vstack("python-showcase-content")
    _append(document, body, content, preferred=Size(620.0, 536.0))
    text_area = document.create_text_area(
        "Long UTF-8 fixture: Привет, native UI — selection, clipping and scrolling\n"
        + "0123456789 abcdefghijklmnopqrstuvwxyz " * 4
    )
    _append(document, content, text_area, preferred=Size(620.0, 82.0))

    collection_model = CollectionModel()
    collection_model.set_items(
        [
            CollectionItem("scene", "Scene hierarchy", "Collection model"),
            CollectionItem("assets", "Asset browser"),
            CollectionItem("disabled", "Unavailable source", enabled=False),
            CollectionItem("build", "Build output"),
        ]
    )
    list_widget = document.create_list_widget(collection_model)
    list_widget.select(1)
    _append(document, content, list_widget, preferred=Size(620.0, 92.0))

    tree_model = TreeModel()
    scene = tree_model.append_root(CollectionItem("scene-root", "Scene"))
    camera = tree_model.append_child(scene, CollectionItem("camera", "Camera"))
    tree_model.append_child(scene, CollectionItem("light", "Key Light"))
    expansion_model = TreeExpansionModel()
    expansion_model.set_expanded(scene, True)
    tree_widget = document.create_tree_widget(tree_model, expansion_model)
    tree_widget.select(camera)
    _append(document, content, tree_widget, preferred=Size(620.0, 92.0))

    table_model = TableModel()
    table_model.set_rows(
        [
            TableRowData("camera", ["Camera", "Scene", "Active"]),
            TableRowData("light", ["Key Light", "Scene", "Active"]),
            TableRowData("mesh", ["Preview Mesh", "Assets", "Imported"]),
        ]
    )
    column_model = TableColumnModel()
    column_model.set_columns(
        [
            TableColumn("name", "Name", TableColumnPolicy.Stretch, min_width=100.0),
            TableColumn("source", "Source", TableColumnPolicy.Stretch, min_width=72.0),
            TableColumn("state", "State", TableColumnPolicy.Fixed, width=88.0),
        ]
    )
    table_widget = document.create_table_widget(table_model, column_model)
    table_widget.select(0)
    _append(document, content, table_widget, preferred=Size(620.0, 116.0))

    frame_model = FrameTimeModel()
    frame_model.max_samples = 120
    frame_model.set_samples([16.0, 16.8, 15.7, 18.2, 16.3])
    frame_graph = document.create_frame_time_graph(frame_model)
    _append(document, content, frame_graph, preferred=Size(620.0, 80.0))

    status = document.create_status_bar("Ready | Python native UI")
    _append(document, root, status, preferred=Size(800.0, 24.0))

    widgets = {
        "toolbar": toolbar,
        "text_input": text_input,
        "text_area": text_area,
        "list": list_widget,
        "tree": tree_widget,
        "table": table_widget,
        "frame_graph": frame_graph,
        "status": status,
    }
    models = (
        command_model,
        collection_model,
        tree_model,
        expansion_model,
        table_model,
        column_model,
        frame_model,
    )
    return PythonShowcase(root=root, widgets=widgets, models=models)


__all__ = ["PythonShowcase", "build_python_showcase"]
