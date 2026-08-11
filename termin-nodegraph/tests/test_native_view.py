import gc
import weakref

import pytest

from tcnodegraph import Graph, GraphController, build_native_node_graph_view
from termin.gui_native import (
    DrawCommandType,
    DrawList,
    EventResult,
    KeyCode,
    KeyEvent,
    KeyEventType,
    PaintContext,
    PointerEvent,
    PointerEventType,
    Rect,
    tc_ui_document_create,
    tc_ui_document_destroy,
)


def _pointer(event_type, x: float, y: float, *, button: int = 0) -> PointerEvent:
    event = PointerEvent()
    event.type = event_type
    event.x = x
    event.y = y
    event.button = button
    return event


def test_native_node_graph_projects_connects_drags_deletes_and_releases():
    graph = Graph()
    controller = GraphController(graph)
    source = controller.create_node("source", title="Source", x=0.0, y=0.0)
    controller.add_output_socket(source.id, "color", "fbo")
    target = controller.create_node("target", title="Target", x=350.0, y=0.0)
    target.params["enabled"] = True
    controller.add_input_socket(target.id, "color", "fbo")
    document = tc_ui_document_create()
    renders = []
    native = build_native_node_graph_view(
        document,
        graph,
        request_render=lambda: renders.append(True),
        controller=controller,
    )
    assert native.controller is controller
    assert document.add_root(native.root.handle)
    document.layout_roots(Rect(0.0, 0.0, 1000.0, 700.0))

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    command_types = [command.type for command in draw_list.commands]
    assert DrawCommandType.Canvas2DList in command_types
    checkbox = native.param_widgets[(target.id, "enabled")]
    assert checkbox.widget.bounds.width > 0.0
    assert checkbox.widget.bounds.height > 0.0
    checkbox.checked = False
    assert target.params["enabled"] is False

    assert document.dispatch_pointer_event(
        _pointer(PointerEventType.Down, 690.0, 356.0)
    ) == EventResult.Handled
    document.dispatch_pointer_event(_pointer(PointerEventType.Move, 850.0, 356.0))
    document.dispatch_pointer_event(_pointer(PointerEventType.Up, 850.0, 356.0))
    assert len(graph.edges) == 1

    document.dispatch_pointer_event(_pointer(PointerEventType.Down, 900.0, 390.0))
    document.dispatch_pointer_event(_pointer(PointerEventType.Move, 930.0, 420.0))
    document.dispatch_pointer_event(_pointer(PointerEventType.Up, 930.0, 420.0))
    assert target.x == 380.0
    assert target.y == 30.0

    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Delete
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert target.id not in graph.nodes
    assert not graph.edges
    assert renders

    native_ref = weakref.ref(native)
    native.close()
    tc_ui_document_destroy(document)
    del native
    gc.collect()
    assert native_ref() is None


def test_native_node_graph_replacement_preserves_supplied_controller_policy():
    class RejectingValidator:
        def validate(self, *_args, **_kwargs):
            return False

    original = Graph()
    validator = RejectingValidator()
    controller = GraphController(original, validator=validator)
    document = tc_ui_document_create()
    native = build_native_node_graph_view(
        document,
        original,
        request_render=lambda: None,
        controller=controller,
    )
    replacement = Graph()

    native.set_graph(replacement)

    assert native.controller is controller
    assert controller.graph is replacement
    assert controller.validator is validator
    native.close()
    tc_ui_document_destroy(document)


def test_native_node_graph_rejects_mismatched_supplied_controller():
    document = tc_ui_document_create()
    with pytest.raises(ValueError, match="does not own"):
        build_native_node_graph_view(
            document,
            Graph(),
            request_render=lambda: None,
            controller=GraphController(Graph()),
        )
    tc_ui_document_destroy(document)
