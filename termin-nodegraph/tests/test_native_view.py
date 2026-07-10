import gc
import weakref

from tcnodegraph import Graph, GraphController, build_native_node_graph_view
from termin.gui_native import (
    Document,
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
    document = Document()
    renders = []
    native = build_native_node_graph_view(
        document,
        graph,
        request_render=lambda: renders.append(True),
    )
    assert document.add_root(native.root.handle)
    document.layout_roots(Rect(0.0, 0.0, 1000.0, 700.0))

    draw_list = DrawList()
    document.paint_roots(PaintContext(draw_list))
    command_types = [command.type for command in draw_list.commands]
    assert command_types.count(DrawCommandType.FillRect) >= 6
    assert DrawCommandType.Text in command_types
    checkbox = native.param_widgets[(target.id, "enabled")]
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

    native.close()
    native_ref = weakref.ref(native)
    del native
    gc.collect()
    assert native_ref() is None
