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
    Point,
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
    assert checkbox.widget.bounds.height == pytest.approx(18.0)
    checkbox.checked = False
    assert target.params["enabled"] is False

    assert document.dispatch_pointer_event(_pointer(PointerEventType.Down, 690.0, 356.0)) == EventResult.Handled
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


def test_native_node_graph_parameter_rows_use_editor_sized_layout():
    graph = Graph()
    controller = GraphController(graph)
    node = controller.create_node("pass", title="Pass")
    node.params.update({"enabled": True, "samples": 4, "quality": "High"})
    node.data["param_specs"] = {
        "enabled": {"kind": "bool"},
        "samples": {"kind": "int"},
        "quality": {"kind": "enum", "items": ["Low", "High"]},
    }
    document = tc_ui_document_create()
    native = build_native_node_graph_view(
        document,
        graph,
        request_render=lambda: None,
        controller=controller,
    )
    assert document.add_root(native.root.handle)
    document.layout_roots(Rect(0.0, 0.0, 1000.0, 700.0))

    enabled = native.param_widgets[(node.id, "enabled")].widget.bounds
    samples = native.param_widgets[(node.id, "samples")].widget.bounds
    quality = native.param_widgets[(node.id, "quality")].widget.bounds
    assert enabled.height == pytest.approx(18.0)
    assert samples.height == pytest.approx(28.0)
    assert quality.height == pytest.approx(28.0)
    samples_before_zoom = Rect(samples.x, samples.y, samples.width, samples.height)

    native.view.set_zoom(2.0, Point(0.0, 0.0))
    document.layout_roots(Rect(0.0, 0.0, 1000.0, 700.0))
    samples_after_zoom = native.param_widgets[(node.id, "samples")].widget.bounds
    assert samples_after_zoom.x == pytest.approx(samples_before_zoom.x)
    assert samples_after_zoom.y == pytest.approx(samples_before_zoom.y)
    assert samples_after_zoom.width == pytest.approx(samples_before_zoom.width)
    assert samples_after_zoom.height == pytest.approx(samples_before_zoom.height)
    assert native.param_widgets[(node.id, "samples")].widget.subtree_transform.scale == pytest.approx(2.0)
    assert samples.y - enabled.y == pytest.approx(25.0)
    assert quality.y - samples.y == pytest.approx(30.0)
    assert native._node_height(node) == pytest.approx(153.0)

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
