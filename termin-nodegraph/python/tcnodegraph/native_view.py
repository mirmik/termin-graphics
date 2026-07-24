"""termin-gui-native projection of the toolkit-neutral node graph model."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field
import math
import weakref

from tcbase import MouseButton
from termin.gui_native import (
    Color,
    GraphicsItem,
    GraphicsScene,
    KeyCode,
    KeyEventType,
    Point,
    PointerEventType,
    Rect,
    Size,
    TcDocument,
)

from tcnodegraph.controller import GraphController
from tcnodegraph.model import Edge, Graph, Node


_BACKGROUND = Color(0.09, 0.10, 0.12, 1.0)
_GRID = Color(0.15, 0.16, 0.20, 1.0)
_AXES = Color(0.24, 0.27, 0.34, 1.0)
_TEXT = Color(0.92, 0.94, 0.98, 1.0)
_PARAM_TEXT = Color(0.70, 0.74, 0.82, 1.0)
_SOCKET_COLORS = {
    "fbo": Color(0.39, 0.70, 0.39, 1.0),
    "color_texture": Color(0.30, 0.62, 0.92, 1.0),
    "depth_texture": Color(0.74, 0.56, 0.28, 1.0),
    "texture": Color(0.78, 0.62, 0.35, 1.0),
    "shadow": Color(0.45, 0.45, 0.70, 1.0),
    "flow": Color(0.88, 0.88, 0.90, 1.0),
    "any": Color(0.68, 0.68, 0.70, 1.0),
}


def _node_palette(node: Node) -> tuple[Color, Color, Color]:
    kind = str(node.data.get("node_type", node.kind))
    graph_type = str(node.data.get("graph_type", node.title))
    if kind == "resource":
        if graph_type == "Shadow Maps":
            return (
                Color(0.22, 0.20, 0.30, 1.0),
                Color(0.34, 0.30, 0.47, 1.0),
                Color(0.48, 0.44, 0.62, 1.0),
            )
        return (
            Color(0.18, 0.24, 0.20, 1.0),
            Color(0.26, 0.37, 0.28, 1.0),
            Color(0.40, 0.52, 0.41, 1.0),
        )
    if kind == "effect":
        return (
            Color(0.28, 0.23, 0.18, 1.0),
            Color(0.42, 0.31, 0.22, 1.0),
            Color(0.56, 0.43, 0.32, 1.0),
        )
    if kind in ("output", "pipeline_output"):
        return (
            Color(0.21, 0.17, 0.24, 1.0),
            Color(0.36, 0.23, 0.42, 1.0),
            Color(0.54, 0.37, 0.62, 1.0),
        )
    return (
        Color(0.17, 0.20, 0.27, 1.0),
        Color(0.24, 0.28, 0.38, 1.0),
        Color(0.32, 0.36, 0.48, 1.0),
    )


def _socket_position(node: Node, socket_name: str, *, output: bool) -> Point | None:
    sockets = node.outputs if output else node.inputs
    for index, socket in enumerate(sockets):
        if socket.name == socket_name:
            return Point(
                node.x + (node.width if output else 0.0),
                node.y + 26.0 + 20.0 * (index + 0.5),
            )
    return None


def _bezier_points(start: Point, end: Point, *, steps: int = 32) -> list[Point]:
    control_span = max(40.0, abs(end.x - start.x) * 0.45)
    result = []
    for index in range(steps + 1):
        t = index / steps
        mt = 1.0 - t
        result.append(
            Point(
                mt**3 * start.x
                + 3.0 * mt * mt * t * (start.x + control_span)
                + 3.0 * mt * t * t * (end.x - control_span)
                + t**3 * end.x,
                mt**3 * start.y
                + 3.0 * mt * mt * t * start.y
                + 3.0 * mt * t * t * end.y
                + t**3 * end.y,
            )
        )
    return result


def _distance_sq_to_segment(point: Point, start: Point, end: Point) -> float:
    vx = end.x - start.x
    vy = end.y - start.y
    length_sq = vx * vx + vy * vy
    if length_sq <= 1.0e-8:
        return (point.x - start.x) ** 2 + (point.y - start.y) ** 2
    projection = ((point.x - start.x) * vx + (point.y - start.y) * vy) / length_sq
    projection = max(0.0, min(1.0, projection))
    closest = Point(start.x + projection * vx, start.y + projection * vy)
    return (point.x - closest.x) ** 2 + (point.y - closest.y) ** 2


@dataclass
class NativeNodeGraphView:
    """Own a native scene projection and graph editing interaction state."""

    document: TcDocument
    graph: Graph
    controller: GraphController
    scene: GraphicsScene
    view: object
    request_render: Callable[[], None]
    node_items: dict[str, GraphicsItem] = field(default_factory=dict)
    edge_items: dict[str, GraphicsItem] = field(default_factory=dict)
    group_items: dict[str, GraphicsItem] = field(default_factory=dict)
    param_widgets: dict[tuple[str, str], object] = field(default_factory=dict)
    on_context_requested: Callable[[Point, str | None], None] | None = None
    on_graph_changed: Callable[[], None] | None = None
    on_param_changed: Callable[[Node, str, object], None] | None = None
    _pending_connection: tuple[str, str, bool] | None = None
    _pending_world: Point | None = None
    _pending_item: GraphicsItem | None = None
    _embedded_items: list[GraphicsItem] = field(default_factory=list)

    @property
    def root(self):
        return self.view.widget

    def set_graph(self, graph: Graph) -> None:
        self.graph = graph
        self.controller = GraphController(graph)
        self._clear_pending()
        self.rebuild()

    def rebuild(self) -> None:
        self._destroy_param_widgets()
        self.scene.clear()
        self.node_items.clear()
        self.edge_items.clear()
        self.group_items.clear()
        for group in self.graph.groups.values():
            item = GraphicsItem(f"group:{group.id}")
            item.position = Point(group.x, group.y)
            item.size = Size(group.width, group.height)
            item.z_index = -20.0
            item.draggable = True
            item.set_paint_callback(self._group_painter(group.title))
            self.scene.add_item(item)
            self.group_items[group.id] = item
        for node in self.graph.nodes.values():
            item = GraphicsItem(f"node:{node.id}")
            item.position = Point(node.x, node.y)
            item.size = Size(node.width, self._node_height(node))
            item.draggable = True
            item.set_paint_callback(self._node_painter(node))
            self.scene.add_item(item)
            self._append_param_widgets(item, node)
            self.node_items[node.id] = item
        for edge in self.graph.edges.values():
            self._append_edge(edge)
        self.view.scene = self.scene
        self.request_render()

    def refresh(self) -> None:
        self.rebuild()

    def close(self) -> None:
        self._clear_pending()
        self._destroy_param_widgets()
        self.view.set_pointer_handler(None)
        self.view.set_key_handler(None)
        self.scene.clear()

    def _append_edge(self, edge: Edge) -> None:
        item = GraphicsItem(f"edge:{edge.id}")
        item.z_index = -10.0
        item.set_paint_callback(self._edge_painter(edge))
        item.set_hit_test_callback(self._edge_hit_tester(edge))
        self.scene.add_item(item)
        self.edge_items[edge.id] = item

    def _node_painter(self, node: Node):
        def paint(item: GraphicsItem, context, transform) -> None:
            position = transform.world_to_screen(item.world_position)
            size = item.size
            width = size.width * transform.zoom
            height = size.height * transform.zoom
            title_height = 26.0 * transform.zoom
            fill, title, border = _node_palette(node)
            if item.selected:
                border = Color(0.70, 0.85, 1.0, 1.0)
            context.fill_rect(Rect(position.x, position.y, width, height), fill)
            context.fill_rect(Rect(position.x, position.y, width, title_height), title)
            context.stroke_rect(Rect(position.x, position.y, width, height), border, 1.5)
            context.draw_text(
                node.title,
                Point(position.x + 8.0, position.y + min(title_height - 4.0, 18.0 * transform.zoom)),
                max(7.0, 13.0 * transform.zoom),
                _TEXT,
            )
            NativeNodeGraphView._paint_sockets(node, context, transform)
            NativeNodeGraphView._paint_param_labels(node, context, transform)

        return paint

    @staticmethod
    def _group_painter(title: str):
        def paint(item: GraphicsItem, context, transform) -> None:
            position = transform.world_to_screen(item.world_position)
            size = item.size
            rect = Rect(
                position.x,
                position.y,
                size.width * transform.zoom,
                size.height * transform.zoom,
            )
            context.fill_rect(rect, Color(0.16, 0.20, 0.30, 0.20))
            context.stroke_rect(
                rect,
                Color(0.70, 0.85, 1.0, 1.0) if item.selected else Color(0.28, 0.40, 0.62, 0.9),
                1.5,
            )
            context.draw_text(title, Point(position.x + 8.0, position.y + 18.0), 12.0, _TEXT)

        return paint

    def _edge_painter(self, edge: Edge):
        weak_owner = weakref.ref(self)

        def paint(item: GraphicsItem, context, transform) -> None:
            owner = weak_owner()
            if owner is None:
                return
            start = owner._edge_endpoint(edge, output=True)
            end = owner._edge_endpoint(edge, output=False)
            if start is None or end is None:
                return
            points = [transform.world_to_screen(point) for point in _bezier_points(start, end)]
            color = owner._edge_color(edge)
            if item.selected:
                color = Color(1.0, 0.85, 0.32, 1.0)
            context.draw_polyline(points, color, 2.6 if item.selected else 1.8)

        return paint

    def _edge_hit_tester(self, edge: Edge):
        weak_owner = weakref.ref(self)

        def hit(_item: GraphicsItem, x: float, y: float) -> bool:
            owner = weak_owner()
            if owner is None:
                return False
            start = owner._edge_endpoint(edge, output=True)
            end = owner._edge_endpoint(edge, output=False)
            if start is None or end is None:
                return False
            point = Point(x, y)
            points = _bezier_points(start, end)
            return any(
                _distance_sq_to_segment(point, points[index], points[index + 1]) <= 100.0
                for index in range(len(points) - 1)
            )

        return hit

    @staticmethod
    def _paint_sockets(node: Node, context, transform) -> None:
        for output, sockets in ((False, node.inputs), (True, node.outputs)):
            for index, socket in enumerate(sockets):
                world = Point(
                    node.x + (node.width if output else 0.0),
                    node.y + 26.0 + 20.0 * (index + 0.5),
                )
                screen = transform.world_to_screen(world)
                size = max(4.0, 8.0 * transform.zoom)
                context.fill_rect(
                    Rect(screen.x - size / 2.0, screen.y - size / 2.0, size, size),
                    _SOCKET_COLORS.get(socket.socket_type, _SOCKET_COLORS["any"]),
                )
                label_x = screen.x + 8.0 if not output else screen.x - node.width * 0.45 * transform.zoom
                context.draw_text(
                    socket.name,
                    Point(label_x, screen.y + 4.0 * transform.zoom),
                    max(6.0, 11.0 * transform.zoom),
                    _TEXT,
                )

    @staticmethod
    def _paint_param_labels(node: Node, context, transform) -> None:
        row_y = node.y + 26.0 + max(len(node.inputs), len(node.outputs), 1) * 20.0 + 8.0
        specs = node.data.get("param_specs", {})
        if not isinstance(specs, dict):
            specs = {}
        for name, _value in node.params.items():
            screen = transform.world_to_screen(Point(node.x + 8.0, row_y + 13.0))
            spec = specs.get(name, {})
            label = str(spec.get("label", name)) if isinstance(spec, dict) else name
            context.draw_text(label, screen, max(6.0, 10.0 * transform.zoom), _PARAM_TEXT)
            row_y += 18.0

    def _append_param_widgets(self, parent: GraphicsItem, node: Node) -> None:
        row_y = 26.0 + max(len(node.inputs), len(node.outputs), 1) * 20.0 + 5.0
        for name, value in node.params.items():
            widget = self._create_param_widget(node, name, value)
            item = GraphicsItem(f"param:{node.id}:{name}")
            item.position = Point(node.width * 0.52, row_y)
            item.size = Size(max(64.0, node.width * 0.46 - 8.0), 18.0)
            item.selectable = False
            item.embedded_widget = widget.handle
            if not parent.add_child(item):
                item.clear_embedded_widget()
                self.document.destroy_widget_recursive(widget.handle)
                raise RuntimeError(f"failed to attach native node parameter '{node.id}.{name}'")
            self._embedded_items.append(item)
            self.param_widgets[(node.id, name)] = widget
            row_y += 18.0

    def _create_param_widget(self, node: Node, name: str, value: object):
        spec = self._param_spec(node, name, value)
        kind = str(spec.get("kind", "string")).lower()
        weak_owner = weakref.ref(self)

        def changed(next_value: object) -> None:
            owner = weak_owner()
            if owner is not None:
                owner._set_param(node.id, name, next_value)

        if kind == "bool":
            widget = self.document.create_checkbox(bool(value))
            widget.connect_changed(changed)
            return widget
        if kind == "enum":
            widget = self.document.create_combo_box()
            values = []
            items = spec.get("items", [])
            if not isinstance(items, list):
                items = []
            selected = -1
            for index, item in enumerate(items):
                if isinstance(item, dict):
                    item_value = str(item.get("value", ""))
                    item_label = str(item.get("label", item_value))
                else:
                    item_value = str(item)
                    item_label = item_value
                values.append(item_value)
                widget.add_item(item_label)
                if item_value == str(value):
                    selected = index
            if selected < 0 and str(value):
                values.append(str(value))
                widget.add_item(str(value))
                selected = len(values) - 1
            widget.selected_index = selected

            def enum_changed(index: int, text: str) -> None:
                changed(values[index] if 0 <= index < len(values) else text)

            widget.connect_changed(enum_changed)
            return widget
        if kind in ("int", "float"):
            widget = self.document.create_spin_box(float(value))
            widget.set_range(float(spec.get("min", -1.0e9)), float(spec.get("max", 1.0e9)))
            widget.step = float(spec.get("step", 1.0 if kind == "int" else 0.1))
            widget.decimals = 0 if kind == "int" else int(spec.get("decimals", 3))
            if kind == "int":
                widget.connect_changed(lambda next_value: changed(int(round(next_value))))
            else:
                widget.connect_changed(lambda next_value: changed(float(next_value)))
            return widget
        widget = self.document.create_text_input(str(value))
        widget.connect_submitted(changed)
        return widget

    @staticmethod
    def _param_spec(node: Node, name: str, value: object) -> dict[str, object]:
        specs = node.data.get("param_specs", {})
        if isinstance(specs, dict):
            spec = specs.get(name)
            if isinstance(spec, dict):
                return dict(spec)
        if isinstance(value, bool):
            return {"kind": "bool", "label": name}
        if isinstance(value, int):
            return {"kind": "int", "label": name}
        if isinstance(value, float):
            return {"kind": "float", "label": name, "decimals": 3}
        return {"kind": "string", "label": name}

    def _set_param(self, node_id: str, name: str, value: object) -> None:
        node = self.graph.nodes.get(node_id)
        if node is None:
            return
        if not self.controller.set_node_param(node_id, name, value):
            return
        if self.on_param_changed is not None:
            self.on_param_changed(node, name, value)
        self._notify_graph_changed()
        self.request_render()

    def _destroy_param_widgets(self) -> None:
        for item in self._embedded_items:
            handle = item.embedded_widget
            item.clear_embedded_widget()
            if self.document.is_alive(handle):
                self.document.destroy_widget_recursive(handle)
        self._embedded_items.clear()
        self.param_widgets.clear()

    def _pointer(self, world: Point, event) -> bool:
        if event.type == PointerEventType.Down and event.button == MouseButton.RIGHT.value:
            if self.on_context_requested is not None:
                hit = self.scene.hit_test(world.x, world.y)
                self.on_context_requested(world, None if hit is None else hit.stable_id)
                return True
            return False
        if event.type == PointerEventType.Down and event.button == MouseButton.LEFT.value:
            socket = self._hit_socket(world)
            if socket is None:
                return False
            self._pending_connection = socket
            self._pending_world = world
            self._install_pending_item()
            self.request_render()
            return True
        if event.type == PointerEventType.Move and self._pending_connection is not None:
            self._pending_world = world
            if self._pending_item is not None:
                self._pending_item.position = Point(world.x, world.y)
            self.request_render()
            return True
        if (event.type == PointerEventType.Up and
                event.button == MouseButton.LEFT.value and
                self._pending_connection is not None):
            start = self._pending_connection
            target = self._hit_socket(world)
            self._clear_pending()
            if target is not None and start[0] != target[0] and start[2] != target[2]:
                if start[2]:
                    source, destination = start, target
                else:
                    source, destination = target, start
                result = self.controller.connect(source[0], source[1], destination[0], destination[1])
                if result.ok:
                    self._notify_graph_changed()
                    self.rebuild()
            self.request_render()
            return True
        return False

    def _key(self, event) -> bool:
        if event.type != KeyEventType.Down or event.key != KeyCode.Delete:
            return False
        removed = False
        for item in tuple(self.scene.selected_items):
            stable_id = item.stable_id
            if stable_id.startswith("node:"):
                removed = self.controller.remove_node(stable_id[5:]) or removed
            elif stable_id.startswith("edge:"):
                removed = self.controller.remove_edge(stable_id[5:]) or removed
            elif stable_id.startswith("group:"):
                removed = self.controller.remove_group(stable_id[6:]) or removed
        if removed:
            self._notify_graph_changed()
            self.rebuild()
        return removed

    def _item_moved(self, item: GraphicsItem) -> None:
        stable_id = item.stable_id
        position = item.position
        if stable_id.startswith("node:"):
            self.controller.move_node(stable_id[5:], position.x, position.y)
        elif stable_id.startswith("group:"):
            group = self.graph.groups.get(stable_id[6:])
            if group is not None:
                group.x = position.x
                group.y = position.y
        self._notify_graph_changed()
        self.request_render()

    def _hit_socket(self, world: Point) -> tuple[str, str, bool] | None:
        radius_sq = 12.0**2
        for node in reversed(tuple(self.graph.nodes.values())):
            for output, sockets in ((False, node.inputs), (True, node.outputs)):
                for socket in sockets:
                    position = _socket_position(node, socket.name, output=output)
                    if position is None:
                        continue
                    if math.dist((world.x, world.y), (position.x, position.y)) ** 2 <= radius_sq:
                        return node.id, socket.name, output
        return None

    def _install_pending_item(self) -> None:
        if self._pending_item is not None:
            self.scene.remove_item(self._pending_item)
        weak_owner = weakref.ref(self)
        item = GraphicsItem("pending-connection")
        item.selectable = False
        item.z_index = -9.0

        def paint(_item: GraphicsItem, context, transform) -> None:
            owner = weak_owner()
            if owner is None or owner._pending_connection is None or owner._pending_world is None:
                return
            node_id, socket_name, output = owner._pending_connection
            node = owner.graph.nodes.get(node_id)
            if node is None:
                return
            start = _socket_position(node, socket_name, output=output)
            if start is None:
                return
            points = [transform.world_to_screen(point) for point in _bezier_points(start, owner._pending_world)]
            context.draw_polyline(points, Color(0.9, 0.86, 0.55, 1.0), 2.0)

        item.set_paint_callback(paint)
        self.scene.add_item(item)
        self._pending_item = item

    def _clear_pending(self) -> None:
        if self._pending_item is not None:
            self.scene.remove_item(self._pending_item)
        self._pending_item = None
        self._pending_connection = None
        self._pending_world = None

    def _edge_endpoint(self, edge: Edge, *, output: bool) -> Point | None:
        node_id = edge.src_node_id if output else edge.dst_node_id
        socket_name = edge.src_socket if output else edge.dst_socket
        node = self.graph.nodes.get(node_id)
        if node is None:
            return None
        return _socket_position(node, socket_name, output=output)

    def _edge_color(self, edge: Edge) -> Color:
        node = self.graph.nodes.get(edge.src_node_id)
        if node is not None:
            for socket in node.outputs:
                if socket.name == edge.src_socket:
                    return _SOCKET_COLORS.get(socket.socket_type, _SOCKET_COLORS["any"])
        return _SOCKET_COLORS["any"]

    @staticmethod
    def _node_height(node: Node) -> float:
        if bool(node.data.get("explicit_size", False)):
            return node.height
        socket_height = max(len(node.inputs), len(node.outputs), 1) * 20.0
        return max(node.height, 26.0 + socket_height + len(node.params) * 18.0 + 12.0)

    def _notify_graph_changed(self) -> None:
        if self.on_graph_changed is not None:
            self.on_graph_changed()


def build_native_node_graph_view(
    document: TcDocument,
    graph: Graph,
    *,
    request_render: Callable[[], None],
) -> NativeNodeGraphView:
    scene = GraphicsScene()
    view = document.create_scene_view(scene)
    view.widget.stable_id = "nodegraph.native-view"
    view.widget.preferred_size = Size(900.0, 650.0)
    view.set_scene_colors(_BACKGROUND, _GRID, _AXES)
    view.offset = Point(500.0, 320.0)
    result = NativeNodeGraphView(
        document=document,
        graph=graph,
        controller=GraphController(graph),
        scene=scene,
        view=view,
        request_render=request_render,
    )
    weak_result = weakref.ref(result)

    def pointer(world: Point, event) -> bool:
        owner = weak_result()
        return False if owner is None else owner._pointer(world, event)

    def key(event) -> bool:
        owner = weak_result()
        return False if owner is None else owner._key(event)

    def moved(item: GraphicsItem) -> None:
        owner = weak_result()
        if owner is not None:
            owner._item_moved(item)

    view.set_pointer_handler(pointer)
    view.set_key_handler(key)
    view.connect_item_moved(moved)
    result.rebuild()
    return result


__all__ = ["NativeNodeGraphView", "build_native_node_graph_view"]
