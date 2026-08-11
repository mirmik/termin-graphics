"""termin-gui-native projection of the toolkit-neutral node graph model."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass, field
import math
import weakref

from tcbase import MouseButton
from termin.gui_native import (
    KeyCode,
    KeyEventType,
    Point,
    SrgbColor,
    PointerEventType,
    Rect,
    Size,
    TcDocument,
)
from termin.visual_scene import (
    GraphicItemRef2D,
    PolylineItemRef2D,
    TcVisualScene,
    tc_visual_scene_create,
    tc_visual_scene_destroy,
)

from tcnodegraph.controller import GraphController
from tcnodegraph.model import Edge, Graph, Node


_BACKGROUND = SrgbColor(0.09, 0.10, 0.12, 1.0)
_GRID = SrgbColor(0.15, 0.16, 0.20, 1.0)
_AXES = SrgbColor(0.24, 0.27, 0.34, 1.0)
_TEXT = SrgbColor(0.92, 0.94, 0.98, 1.0)
_PARAM_TEXT = SrgbColor(0.70, 0.74, 0.82, 1.0)
_SOCKET_COLORS = {
    "fbo": SrgbColor(0.39, 0.70, 0.39, 1.0),
    "color_texture": SrgbColor(0.30, 0.62, 0.92, 1.0),
    "depth_texture": SrgbColor(0.74, 0.56, 0.28, 1.0),
    "texture": SrgbColor(0.78, 0.62, 0.35, 1.0),
    "shadow": SrgbColor(0.45, 0.45, 0.70, 1.0),
    "flow": SrgbColor(0.88, 0.88, 0.90, 1.0),
    "any": SrgbColor(0.68, 0.68, 0.70, 1.0),
}


def _color(value: SrgbColor) -> SrgbColor:
    return value


def _rect(value: Rect) -> tuple[float, float, float, float]:
    return value.x, value.y, value.width, value.height


def _point(value: Point) -> tuple[float, float]:
    return value.x, value.y


def _points(values: list[Point]) -> list[tuple[float, float]]:
    return [_point(value) for value in values]


def _node_palette(node: Node) -> tuple[SrgbColor, SrgbColor, SrgbColor]:
    kind = str(node.data.get("node_type", node.kind))
    graph_type = str(node.data.get("graph_type", node.title))
    if kind == "resource":
        if graph_type == "Shadow Maps":
            return (
                SrgbColor(0.22, 0.20, 0.30, 1.0),
                SrgbColor(0.34, 0.30, 0.47, 1.0),
                SrgbColor(0.48, 0.44, 0.62, 1.0),
            )
        return (
            SrgbColor(0.18, 0.24, 0.20, 1.0),
            SrgbColor(0.26, 0.37, 0.28, 1.0),
            SrgbColor(0.40, 0.52, 0.41, 1.0),
        )
    if kind == "effect":
        return (
            SrgbColor(0.28, 0.23, 0.18, 1.0),
            SrgbColor(0.42, 0.31, 0.22, 1.0),
            SrgbColor(0.56, 0.43, 0.32, 1.0),
        )
    if kind in ("output", "pipeline_output"):
        return (
            SrgbColor(0.21, 0.17, 0.24, 1.0),
            SrgbColor(0.36, 0.23, 0.42, 1.0),
            SrgbColor(0.54, 0.37, 0.62, 1.0),
        )
    return (
        SrgbColor(0.17, 0.20, 0.27, 1.0),
        SrgbColor(0.24, 0.28, 0.38, 1.0),
        SrgbColor(0.32, 0.36, 0.48, 1.0),
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


@dataclass
class NativeNodeGraphView:
    """Own a native scene projection and graph editing interaction state."""

    document: TcDocument
    controller: GraphController
    scene: TcVisualScene
    view: object
    request_render: Callable[[], None]
    node_items: dict[str, GraphicItemRef2D] = field(default_factory=dict)
    edge_items: dict[str, PolylineItemRef2D] = field(default_factory=dict)
    group_items: dict[str, GraphicItemRef2D] = field(default_factory=dict)
    param_widgets: dict[tuple[str, str], object] = field(default_factory=dict)
    on_context_requested: Callable[[Point, str | None], None] | None = None
    on_graph_changed: Callable[[], None] | None = None
    on_param_changed: Callable[[Node, str, object], None] | None = None
    _pending_connection: tuple[str, str, bool] | None = None
    _pending_world: Point | None = None
    _pending_item: PolylineItemRef2D | None = None
    _embedded_items: list[tuple[GraphicItemRef2D, object]] = field(default_factory=list)
    _semantic_ids: dict[tuple[int, int, int], str] = field(default_factory=dict)
    _selected_ids: set[str] = field(default_factory=set)
    _drag_item: GraphicItemRef2D | None = None
    _drag_id: str | None = None
    _drag_start_world: Point | None = None
    _drag_start_position: tuple[float, float] | None = None

    @property
    def root(self):
        return self.view.widget

    @property
    def graph(self) -> Graph:
        return self.controller.graph

    def set_graph(self, graph: Graph) -> None:
        self.controller.replace_graph(graph)
        self._clear_pending()
        self.rebuild()

    def rebuild(self) -> None:
        self._destroy_param_widgets()
        self.scene.clear()
        self.node_items.clear()
        self.edge_items.clear()
        self.group_items.clear()
        self._semantic_ids.clear()
        self._selected_ids.clear()
        self._cancel_drag()
        for group in self.graph.groups.values():
            item = self.scene.create_rect(
                _rect(Rect(0.0, 0.0, group.width, group.height)),
                _color(SrgbColor(0.16, 0.20, 0.30, 0.20)),
                _color(SrgbColor(0.28, 0.40, 0.62, 0.9)),
                1.5,
            )
            item.position = (group.x, group.y)
            item.z_order = -20
            label = self.scene.create_text(
                group.title,
                (8.0, 18.0),
                12.0,
                _color(_TEXT),
                _rect(Rect(8.0, 4.0, max(1.0, group.width - 16.0), 18.0)),
                item,
            )
            del label
            self.group_items[group.id] = item
            self._register_semantic(item, f"group:{group.id}")
        for node in self.graph.nodes.values():
            height = self._node_height(node)
            fill, title, border = _node_palette(node)
            item = self.scene.create_rounded_rect(
                _rect(Rect(0.0, 0.0, node.width, height)),
                5.0,
                _color(fill),
                _color(border),
                1.5,
            )
            item.position = (node.x, node.y)
            title_item = self.scene.create_rect(
                _rect(Rect(0.0, 0.0, node.width, 26.0)),
                _color(title),
                None,
                1.0,
                item,
            )
            del title_item
            title_text = self.scene.create_text(
                node.title,
                (8.0, 18.0),
                13.0,
                _color(_TEXT),
                _rect(Rect(8.0, 3.0, max(1.0, node.width - 16.0), 20.0)),
                item,
            )
            del title_text
            self._append_socket_visuals(item, node)
            self._append_param_labels(item, node)
            self._append_param_widgets(item, node)
            self.node_items[node.id] = item
            self._register_semantic(item, f"node:{node.id}")
        for edge in self.graph.edges.values():
            self._append_edge(edge)
        self.view.invalidate_scene()
        self.request_render()

    def refresh(self) -> None:
        self.rebuild()

    def close(self) -> None:
        self._clear_pending()
        self._destroy_param_widgets()
        self.view.set_pointer_handler(None)
        self.view.set_key_handler(None)
        self.scene.clear()
        self.view.invalidate_scene()
        tc_visual_scene_destroy(self.scene)

    def _append_edge(self, edge: Edge) -> None:
        points = self._edge_points(edge)
        item = self.scene.create_polyline(
            _points(points),
            _color(self._edge_color(edge)),
            2.6,
        )
        item.z_order = -10
        self.edge_items[edge.id] = item
        self._register_semantic(item, f"edge:{edge.id}")

    def _append_socket_visuals(self, parent: GraphicItemRef2D, node: Node) -> None:
        for output, sockets in ((False, node.inputs), (True, node.outputs)):
            for index, socket in enumerate(sockets):
                local = Point(
                    node.width if output else 0.0,
                    26.0 + 20.0 * (index + 0.5),
                )
                marker = self.scene.create_ellipse(
                    _rect(Rect(local.x - 4.0, local.y - 4.0, 8.0, 8.0)),
                    _color(_SOCKET_COLORS.get(socket.socket_type, _SOCKET_COLORS["any"])),
                    None,
                    1.0,
                    parent,
                )
                del marker
                label_x = local.x + 8.0 if not output else local.x - node.width * 0.45
                label = self.scene.create_text(
                    socket.name,
                    (label_x, local.y + 4.0),
                    11.0,
                    _color(_TEXT),
                    _rect(Rect(label_x, local.y - 8.0, node.width * 0.4, 16.0)),
                    parent,
                )
                del label

    def _append_param_labels(self, parent: GraphicItemRef2D, node: Node) -> None:
        row_y = 26.0 + max(len(node.inputs), len(node.outputs), 1) * 20.0 + 8.0
        specs = node.data.get("param_specs", {})
        if not isinstance(specs, dict):
            specs = {}
        for name, _value in node.params.items():
            spec = specs.get(name, {})
            label = str(spec.get("label", name)) if isinstance(spec, dict) else name
            item = self.scene.create_text(
                label,
                (8.0, row_y + 13.0),
                10.0,
                _color(_PARAM_TEXT),
                _rect(Rect(8.0, row_y, node.width * 0.45, 16.0)),
                parent,
            )
            del item
            row_y += 18.0

    def _append_param_widgets(self, parent: GraphicItemRef2D, node: Node) -> None:
        row_y = 26.0 + max(len(node.inputs), len(node.outputs), 1) * 20.0 + 5.0
        for name, value in node.params.items():
            widget = self._create_param_widget(node, name, value)
            item = self.scene.create_hit_region_rect(
                _rect(Rect(0.0, 0.0, max(64.0, node.width * 0.46 - 8.0), 18.0)),
                parent,
            )
            item.position = (node.width * 0.52, row_y)
            if not self.view.set_widget_portal(item.handle, widget.handle):
                self.scene.destroy(item)
                self.document.destroy_widget_recursive(widget.handle)
                raise RuntimeError(f"failed to attach native node parameter '{node.id}.{name}'")
            self._embedded_items.append((item, widget))
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
        for item, widget in self._embedded_items:
            self.view.clear_widget_portal(item.handle)
            handle = widget.handle
            if self.document.is_alive(handle):
                self.document.destroy_widget_recursive(handle)
        self._embedded_items.clear()
        self.param_widgets.clear()

    def _pointer(self, world: Point, event) -> bool:
        if event.type == PointerEventType.Down and event.button == MouseButton.RIGHT.value:
            if self.on_context_requested is not None:
                hit = self.scene.hit_test(world.x, world.y)
                semantic = self._semantic_item(hit)
                self.on_context_requested(
                    world,
                    None if semantic is None else semantic[0],
                )
                return True
            return False
        if event.type == PointerEventType.Down and event.button == MouseButton.LEFT.value:
            socket = self._hit_socket(world)
            if socket is not None:
                self._pending_connection = socket
                self._pending_world = world
                self._install_pending_item()
                self.request_render()
                return True
            semantic = self._semantic_item(
                self.scene.hit_test(world.x, world.y)
            )
            self._selected_ids.clear()
            if semantic is None:
                self._cancel_drag()
                return False
            stable_id, item = semantic
            self._selected_ids.add(stable_id)
            if stable_id.startswith(("node:", "group:")):
                self._drag_id = stable_id
                self._drag_item = item
                self._drag_start_world = world
                self._drag_start_position = item.position
            self.request_render()
            return True
        if event.type == PointerEventType.Move and self._pending_connection is not None:
            self._pending_world = world
            if self._pending_item is not None:
                self._update_pending_item()
            self.view.invalidate_scene()
            self.request_render()
            return True
        if (
            event.type == PointerEventType.Move
            and self._drag_item is not None
            and self._drag_start_world is not None
            and self._drag_start_position is not None
        ):
            self._drag_item.position = (
                self._drag_start_position[0]
                + world.x - self._drag_start_world.x,
                self._drag_start_position[1]
                + world.y - self._drag_start_world.y,
            )
            self._item_moved(self._drag_item, self._drag_id)
            self.view.invalidate_scene()
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
        if (
            event.type in (PointerEventType.Up, PointerEventType.Cancel)
            and self._drag_item is not None
        ):
            self._cancel_drag()
            return True
        return False

    def _key(self, event) -> bool:
        if event.type != KeyEventType.Down or event.key != KeyCode.Delete:
            return False
        removed = False
        for stable_id in tuple(self._selected_ids):
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

    def _item_moved(
        self,
        item: GraphicItemRef2D,
        stable_id: str | None,
    ) -> None:
        if stable_id is None:
            return
        position_x, position_y = item.position
        if stable_id.startswith("node:"):
            self.controller.move_node(stable_id[5:], position_x, position_y)
        elif stable_id.startswith("group:"):
            group = self.graph.groups.get(stable_id[6:])
            if group is not None:
                group.x = position_x
                group.y = position_y
        self._refresh_edges()
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
            self.scene.destroy(self._pending_item)
        item = self.scene.create_polyline(
            [(0.0, 0.0), (0.0, 0.0)],
            _color(SrgbColor(0.9, 0.86, 0.55, 1.0)),
            2.0,
        )
        item.z_order = -9
        self._pending_item = item
        self._update_pending_item()

    def _update_pending_item(self) -> None:
        if (self._pending_item is None or self._pending_connection is None
                or self._pending_world is None):
            return
        node_id, socket_name, output = self._pending_connection
        node = self.graph.nodes.get(node_id)
        if node is None:
            return
        start = _socket_position(node, socket_name, output=output)
        if start is not None:
            self._pending_item.set(
                _points(_bezier_points(start, self._pending_world)),
                _color(SrgbColor(0.9, 0.86, 0.55, 1.0)),
                2.0,
            )
            self.view.invalidate_scene()

    def _clear_pending(self) -> None:
        if self._pending_item is not None:
            self.scene.destroy(self._pending_item)
        self._pending_item = None
        self._pending_connection = None
        self._pending_world = None
        self.view.invalidate_scene()

    def _edge_endpoint(self, edge: Edge, *, output: bool) -> Point | None:
        node_id = edge.src_node_id if output else edge.dst_node_id
        socket_name = edge.src_socket if output else edge.dst_socket
        node = self.graph.nodes.get(node_id)
        if node is None:
            return None
        return _socket_position(node, socket_name, output=output)

    def _edge_points(self, edge: Edge) -> list[Point]:
        start = self._edge_endpoint(edge, output=True)
        end = self._edge_endpoint(edge, output=False)
        if start is None or end is None:
            return [Point(0.0, 0.0), Point(0.0, 0.0)]
        return _bezier_points(start, end)

    def _refresh_edges(self) -> None:
        for edge_id, item in self.edge_items.items():
            edge = self.graph.edges.get(edge_id)
            if edge is not None:
                item.set(
                    _points(self._edge_points(edge)),
                    _color(self._edge_color(edge)),
                    2.6,
                )
        self.view.invalidate_scene()

    @staticmethod
    def _handle_key(item: GraphicItemRef2D) -> tuple[int, int, int]:
        handle = item.handle
        return handle.scene_id, handle.index, handle.generation

    def _register_semantic(
        self,
        item: GraphicItemRef2D,
        stable_id: str,
    ) -> None:
        self._semantic_ids[self._handle_key(item)] = stable_id

    def _semantic_item(
        self,
        item: GraphicItemRef2D | None,
    ) -> tuple[str, GraphicItemRef2D] | None:
        current = item
        while current is not None:
            stable_id = self._semantic_ids.get(
                self._handle_key(current)
            )
            if stable_id is not None:
                return stable_id, current
            current = current.parent
        return None

    def _cancel_drag(self) -> None:
        self._drag_item = None
        self._drag_id = None
        self._drag_start_world = None
        self._drag_start_position = None

    def _edge_color(self, edge: Edge) -> SrgbColor:
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
    controller: GraphController | None = None,
) -> NativeNodeGraphView:
    graph_controller = controller if controller is not None else GraphController(graph)
    if graph_controller.graph is not graph:
        raise ValueError("supplied GraphController does not own the supplied graph")
    scene = tc_visual_scene_create()
    view = document.create_scene_view(scene)
    view.widget.stable_id = "nodegraph.native-view"
    view.widget.preferred_size = Size(900.0, 650.0)
    view.set_scene_colors(_BACKGROUND, _GRID, _AXES)
    view.offset = Point(500.0, 320.0)
    result = NativeNodeGraphView(
        document=document,
        controller=graph_controller,
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

    view.set_pointer_handler(pointer)
    view.set_key_handler(key)
    result.rebuild()
    return result


__all__ = ["NativeNodeGraphView", "build_native_node_graph_view"]
