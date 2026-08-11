import gc
import weakref

import pytest

from tcnodegraph import (
    Graph,
    GraphController,
    NodeBodyContent,
    NodeBodyLayout,
    build_native_node_graph_view,
)
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
    controller.set_node_param(target.id, "enabled", True)
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
    assert graph.nodes[target.id].params["enabled"] is False

    unregistered_key = KeyEvent()
    unregistered_key.type = KeyEventType.Down
    unregistered_key.raw_key = 61
    assert document.dispatch_key_event(unregistered_key) == EventResult.Ignored

    assert document.dispatch_pointer_event(_pointer(PointerEventType.Down, 690.0, 356.0)) == EventResult.Handled
    document.dispatch_pointer_event(_pointer(PointerEventType.Move, 850.0, 356.0))
    document.dispatch_pointer_event(_pointer(PointerEventType.Up, 850.0, 356.0))
    assert len(graph.edges) == 1

    assert document.dispatch_pointer_event(_pointer(PointerEventType.Down, 690.0, 356.0)) == EventResult.Handled
    document.dispatch_pointer_event(_pointer(PointerEventType.Move, 850.0, 356.0))
    document.dispatch_pointer_event(_pointer(PointerEventType.Up, 850.0, 356.0))
    assert len(graph.edges) == 1

    document.dispatch_pointer_event(_pointer(PointerEventType.Down, 900.0, 390.0))
    document.dispatch_pointer_event(_pointer(PointerEventType.Move, 930.0, 420.0))
    document.dispatch_pointer_event(_pointer(PointerEventType.Up, 930.0, 420.0))
    assert graph.nodes[target.id].x == 380.0
    assert graph.nodes[target.id].y == 30.0

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


def test_native_node_graph_selects_connection_with_tolerance_and_deletes_it():
    graph = Graph()
    controller = GraphController(graph)
    source = controller.create_node("source", title="Source", x=0.0, y=0.0)
    controller.add_output_socket(source.id, "value", "float")
    target = controller.create_node("target", title="Target", x=350.0, y=0.0)
    controller.add_input_socket(target.id, "value", "float")
    connection = controller.connect(source.id, "value", target.id, "value")
    assert connection.ok

    document = tc_ui_document_create()
    native = build_native_node_graph_view(
        document,
        graph,
        request_render=lambda: None,
        controller=controller,
    )
    assert document.add_root(native.root.handle)
    document.layout_roots(Rect(0.0, 0.0, 1000.0, 700.0))
    edge_item = native.edge_items[connection.edge_id]
    normal_bounds = edge_item.local_bounds

    # Six screen pixels from the visible stroke is still an intentional hit.
    assert document.dispatch_pointer_event(
        _pointer(PointerEventType.Down, 770.0, 362.0)
    ) == EventResult.Handled
    selected_bounds = edge_item.local_bounds
    assert selected_bounds[3] - selected_bounds[1] == pytest.approx(5.0)
    assert selected_bounds[3] - selected_bounds[1] > normal_bounds[3] - normal_bounds[1]

    document.dispatch_pointer_event(_pointer(PointerEventType.Down, 750.0, 450.0))
    document.dispatch_pointer_event(_pointer(PointerEventType.Up, 750.0, 450.0))
    cleared_bounds = edge_item.local_bounds
    assert cleared_bounds[3] - cleared_bounds[1] == pytest.approx(2.6)

    assert document.dispatch_pointer_event(
        _pointer(PointerEventType.Down, 770.0, 362.0)
    ) == EventResult.Handled
    key = KeyEvent()
    key.type = KeyEventType.Down
    key.key = KeyCode.Delete
    assert document.dispatch_key_event(key) == EventResult.Handled
    assert not graph.edges
    assert set(graph.nodes) == {source.id, target.id}

    native.close()
    tc_ui_document_destroy(document)


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
    for name, value in {"enabled": True, "samples": 4, "quality": "High"}.items():
        controller.set_node_param(node.id, name, value)
    controller.set_node_data(node.id, {"param_specs": {
        "enabled": {"kind": "bool"},
        "samples": {"kind": "int"},
        "quality": {"kind": "enum", "items": ["Low", "High"]},
    }})
    node = graph.nodes[node.id]
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


def test_native_node_body_portal_retains_widget_and_replaces_anchor_on_rebuild():
    graph = Graph()
    controller = GraphController(graph)
    node = controller.create_node("preview", title="Preview")
    document = tc_ui_document_create()
    updates = []
    provider_calls = []

    def provide(owner_document, snapshot):
        provider_calls.append(snapshot.id)
        widget = owner_document.create_button("Body state")
        return NodeBodyContent(
            widget.widget,
            NodeBodyLayout(
                height=48.0,
                inset_left=11.0,
                inset_right=13.0,
                inset_top=3.0,
                inset_bottom=5.0,
                gap_before=9.0,
            ),
            update=lambda next_snapshot: updates.append(next_snapshot.title),
        )

    native = build_native_node_graph_view(
        document,
        graph,
        request_render=lambda: None,
        controller=controller,
        body_content_provider=provide,
    )
    assert document.add_root(native.root.handle)
    document.layout_roots(Rect(0.0, 0.0, 1000.0, 700.0))

    widget = native.body_widgets[node.id]
    widget_handle = widget.handle
    first_anchor = native.body_items[node.id].handle
    assert provider_calls == [node.id]
    assert native.body_items[node.id].position == pytest.approx((11.0, 65.0))
    assert native._node_height(graph.nodes[node.id]) == pytest.approx(128.0)

    controller.update_node(node.id, title="Updated")
    native.rebuild()
    document.layout_roots(Rect(0.0, 0.0, 1000.0, 700.0))

    assert provider_calls == [node.id]
    assert updates == ["Updated"]
    assert native.body_widgets[node.id] is widget
    assert native.body_widgets[node.id].handle == widget_handle
    assert native.body_items[node.id].handle != first_anchor
    assert document.is_alive(widget_handle)

    native.close()
    native.close()
    assert not document.is_alive(widget_handle)
    tc_ui_document_destroy(document)


def test_native_node_body_portal_retires_removed_nodes_and_set_graph_contents():
    graph = Graph()
    controller = GraphController(graph)
    first = controller.create_node("first")
    second = controller.create_node("second")
    document = tc_ui_document_create()

    def provide(owner_document, snapshot):
        return NodeBodyContent(
            owner_document.create_label(snapshot.kind).widget,
            NodeBodyLayout(height=30.0),
        )

    native = build_native_node_graph_view(
        document,
        graph,
        request_render=lambda: None,
        controller=controller,
        body_content_provider=provide,
    )
    first_handle = native.body_widgets[first.id].handle
    second_handle = native.body_widgets[second.id].handle

    assert controller.remove_node(first.id)
    native.rebuild()
    assert not document.is_alive(first_handle)
    assert document.is_alive(second_handle)

    replacement = Graph()
    replacement_controller = GraphController(replacement)
    replacement_node = replacement_controller.create_node("replacement")
    native.set_graph(replacement)
    assert not document.is_alive(second_handle)
    assert document.is_alive(native.body_widgets[replacement_node.id].handle)

    native.close()
    tc_ui_document_destroy(document)


def test_native_node_body_ignored_left_down_does_not_start_node_drag():
    graph = Graph()
    controller = GraphController(graph)
    node = controller.create_node("body", x=0.0, y=0.0)
    document = tc_ui_document_create()

    native = build_native_node_graph_view(
        document,
        graph,
        request_render=lambda: None,
        controller=controller,
        body_content_provider=lambda owner_document, _node: NodeBodyContent(
            owner_document.create_label("non-interactive body").widget,
            NodeBodyLayout(height=50.0),
        ),
    )
    assert document.add_root(native.root.handle)
    document.layout_roots(Rect(0.0, 0.0, 1000.0, 700.0))

    assert document.dispatch_pointer_event(
        _pointer(PointerEventType.Down, 520.0, 395.0)
    ) == EventResult.Handled
    document.dispatch_pointer_event(_pointer(PointerEventType.Move, 570.0, 435.0))
    document.dispatch_pointer_event(_pointer(PointerEventType.Up, 570.0, 435.0))
    assert graph.nodes[node.id].x == 0.0
    assert graph.nodes[node.id].y == 0.0

    native.close()
    tc_ui_document_destroy(document)


def test_native_node_body_rejects_cross_document_and_parented_widgets():
    graph = Graph()
    controller = GraphController(graph)
    controller.create_node("body")
    document = tc_ui_document_create()
    other_document = tc_ui_document_create()
    foreign = other_document.create_label("foreign")

    with pytest.raises(ValueError, match="another document"):
        build_native_node_graph_view(
            document,
            graph,
            request_render=lambda: None,
            controller=controller,
            body_content_provider=lambda _document, _node: NodeBodyContent(
                foreign.widget,
                NodeBodyLayout(height=30.0),
            ),
        )
    assert foreign.alive

    parent = document.create_hstack()
    child = document.create_label("attached")
    assert parent.widget.append_child(child.widget)
    with pytest.raises(ValueError, match="parentless"):
        build_native_node_graph_view(
            document,
            graph,
            request_render=lambda: None,
            controller=controller,
            body_content_provider=lambda _document, _node: NodeBodyContent(
                child.widget,
                NodeBodyLayout(height=30.0),
            ),
        )
    assert child.alive

    tc_ui_document_destroy(other_document)
    tc_ui_document_destroy(document)


def test_native_node_body_rejects_overflow_from_explicit_node_height():
    graph = Graph()
    controller = GraphController(graph)
    node = controller.create_node("body")
    controller.update_node(
        node.id,
        height=100.0,
        data={"explicit_size": True},
    )
    document = tc_ui_document_create()

    with pytest.raises(ValueError, match="does not fit explicit node height"):
        build_native_node_graph_view(
            document,
            graph,
            request_render=lambda: None,
            controller=controller,
            body_content_provider=lambda owner_document, _node: NodeBodyContent(
                owner_document.create_label("too tall").widget,
                NodeBodyLayout(height=80.0),
            ),
        )

    tc_ui_document_destroy(document)
