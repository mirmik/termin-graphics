"""tcgui scene adapter for Graph model."""

from __future__ import annotations

import logging
from typing import Any

from tcbase import Key, MouseButton
from tcgui.widgets.input_dialog import show_input_dialog
from tcgui.widgets.menu import Menu
from tcgui.widgets.checkbox import Checkbox
from tcgui.widgets.combo_box import ComboBox
from tcgui.widgets.spin_box import SpinBox
from tcgui.widgets.text_input import TextInput
from tcgui.scene import GraphicsItem, GraphicsScene, GraphicsWidgetItem, RectItem, SceneTransform, SceneView

from tcnodegraph.controller import GraphController
from tcnodegraph.model import Graph, Node


def _draw_bezier_connection(
    renderer,
    sx: float,
    sy: float,
    ex: float,
    ey: float,
    color: tuple[float, float, float, float],
    *,
    base_thickness: float = 2.0,
) -> None:
    """Draw a smooth cubic connection using adaptive polyline sampling."""
    dx = abs(ex - sx)
    dy = abs(ey - sy)
    span = dx + dy
    steps = int(max(28, min(140, span / 14.0)))

    cspan = max(40.0, dx * 0.45)
    cx1 = sx + cspan
    cy1 = sy
    cx2 = ex - cspan
    cy2 = ey

    points: list[tuple[float, float]] = []
    for i in range(steps + 1):
        t = i / steps
        mt = 1.0 - t
        x = mt * mt * mt * sx + 3 * mt * mt * t * cx1 + 3 * mt * t * t * cx2 + t * t * t * ex
        y = mt * mt * mt * sy + 3 * mt * mt * t * cy1 + 3 * mt * t * t * cy2 + t * t * t * ey
        points.append((x, y))

    # Soft under-stroke to visually hide jagged edges.
    under = (color[0], color[1], color[2], min(0.35, color[3] * 0.45))
    for i in range(len(points) - 1):
        ax, ay = points[i]
        bx, by = points[i + 1]
        renderer.draw_line(ax, ay, bx, by, under, base_thickness + 1.5)

    # Main stroke.
    for i in range(len(points) - 1):
        ax, ay = points[i]
        bx, by = points[i + 1]
        renderer.draw_line(ax, ay, bx, by, color, base_thickness)


def _bezier_points(
    sx: float,
    sy: float,
    ex: float,
    ey: float,
) -> list[tuple[float, float]]:
    """Return adaptive polyline approximation for cubic connection."""
    dx = abs(ex - sx)
    dy = abs(ey - sy)
    span = dx + dy
    steps = int(max(28, min(140, span / 14.0)))

    cspan = max(40.0, dx * 0.45)
    cx1 = sx + cspan
    cy1 = sy
    cx2 = ex - cspan
    cy2 = ey

    points: list[tuple[float, float]] = []
    for i in range(steps + 1):
        t = i / steps
        mt = 1.0 - t
        x = mt * mt * mt * sx + 3 * mt * mt * t * cx1 + 3 * mt * t * t * cx2 + t * t * t * ex
        y = mt * mt * mt * sy + 3 * mt * mt * t * cy1 + 3 * mt * t * t * cy2 + t * t * t * ey
        points.append((x, y))
    return points


def _distance_sq_point_segment(
    px: float,
    py: float,
    ax: float,
    ay: float,
    bx: float,
    by: float,
) -> float:
    vx = bx - ax
    vy = by - ay
    wx = px - ax
    wy = py - ay
    vv = vx * vx + vy * vy
    if vv < 1e-8:
        dx = px - ax
        dy = py - ay
        return dx * dx + dy * dy
    t = (wx * vx + wy * vy) / vv
    t = max(0.0, min(1.0, t))
    cx = ax + t * vx
    cy = ay + t * vy
    dx = px - cx
    dy = py - cy
    return dx * dx + dy * dy


class GroupItem(RectItem):
    """Visual group frame."""

    def __init__(self, title: str) -> None:
        super().__init__(title)
        self.selectable = True
        self.draggable = True
        self.fill_color = (0.16, 0.20, 0.30, 0.20)
        self.border_color = (0.28, 0.40, 0.62, 0.9)
        self.border_selected_color = (0.70, 0.85, 1.0, 1.0)


class NodeItem(RectItem):
    """Visual item for graph node."""

    def __init__(self, node_id: str, title: str) -> None:
        super().__init__(title)
        self.node_id = node_id
        self.selectable = True
        self.draggable = True
        self.fill_color = (0.17, 0.20, 0.27, 1.0)
        self.border_color = (0.32, 0.36, 0.48, 1.0)
        self.title_bg_color = (0.24, 0.28, 0.38, 1.0)
        self.input_socket_color = (0.52, 0.72, 0.52, 1.0)
        self.output_socket_color = (0.74, 0.68, 0.48, 1.0)
        self.title_height = 26.0
        self.socket_row_height = 20.0
        self.socket_size = 8.0
        self.label_font_size = 12.0
        self.param_row_height = 18.0
        self.param_label_color = (0.70, 0.74, 0.82, 1.0)
        self.param_value_color = (0.92, 0.94, 0.98, 1.0)
        self.draw_param_names = True
        self.draw_param_values = True
        self._socket_colors = {
            "fbo": (0.39, 0.70, 0.39, 1.0),
            "color_texture": (0.30, 0.62, 0.92, 1.0),
            "depth_texture": (0.74, 0.56, 0.28, 1.0),
            "texture": (0.78, 0.62, 0.35, 1.0),
            "shadow": (0.45, 0.45, 0.70, 1.0),
            "any": (0.68, 0.68, 0.70, 1.0),
            "flow": (0.88, 0.88, 0.90, 1.0),
        }

    @property
    def node(self) -> Node:
        return self.data["node"]

    def configure_palette(self, node_kind: str, graph_type: str) -> None:
        if node_kind == "resource":
            if graph_type == "Shadow Maps":
                self.fill_color = (0.22, 0.20, 0.30, 1.0)
                self.title_bg_color = (0.34, 0.30, 0.47, 1.0)
                self.border_color = (0.48, 0.44, 0.62, 1.0)
            else:
                self.fill_color = (0.18, 0.24, 0.20, 1.0)
                self.title_bg_color = (0.26, 0.37, 0.28, 1.0)
                self.border_color = (0.40, 0.52, 0.41, 1.0)
        elif node_kind == "effect":
            self.fill_color = (0.28, 0.23, 0.18, 1.0)
            self.title_bg_color = (0.42, 0.31, 0.22, 1.0)
            self.border_color = (0.56, 0.43, 0.32, 1.0)
        elif node_kind == "viewport":
            self.fill_color = (0.28, 0.19, 0.22, 1.0)
            self.title_bg_color = (0.44, 0.26, 0.33, 1.0)
            self.border_color = (0.60, 0.38, 0.46, 1.0)
        elif node_kind == "output":
            self.fill_color = (0.21, 0.17, 0.24, 1.0)
            self.title_bg_color = (0.36, 0.23, 0.42, 1.0)
            self.border_color = (0.54, 0.37, 0.62, 1.0)
        else:
            self.fill_color = (0.17, 0.20, 0.27, 1.0)
            self.title_bg_color = (0.24, 0.28, 0.38, 1.0)
            self.border_color = (0.32, 0.36, 0.48, 1.0)

    def _socket_section_height(self) -> float:
        return max(len(self.node.inputs), len(self.node.outputs), 1) * self.socket_row_height

    def _params_start_y(self) -> float:
        return self.title_height + self._socket_section_height() + 4.0

    def content_min_height(self) -> float:
        params_h = len(self.node.params) * self.param_row_height
        return self._params_start_y() + params_h + 8.0

    def socket_world_pos(self, socket_name: str, *, output: bool) -> tuple[float, float] | None:
        sockets = self.node.outputs if output else self.node.inputs
        for i, sock in enumerate(sockets):
            if sock.name != socket_name:
                continue
            sx = self.x + self.width if output else self.x
            sy = self.y + self.title_height + self.socket_row_height * (i + 0.5)
            return (sx, sy)
        return None

    def hit_socket(self, wx: float, wy: float) -> tuple[str, str] | None:
        x, y = self.world_position()
        lx = wx - x
        ly = wy - y
        if lx < -self.socket_size * 2 or lx > self.width + self.socket_size * 2:
            return None
        if ly < self.title_height - 2:
            return None

        radius2 = (self.socket_size * 1.25) ** 2
        for sock in self.node.inputs:
            pos = self.socket_world_pos(sock.name, output=False)
            if pos is None:
                continue
            sx, sy = pos
            dx = wx - sx
            dy = wy - sy
            if dx * dx + dy * dy <= radius2:
                return ("input", sock.name)

        for sock in self.node.outputs:
            pos = self.socket_world_pos(sock.name, output=True)
            if pos is None:
                continue
            sx, sy = pos
            dx = wx - sx
            dy = wy - sy
            if dx * dx + dy * dy <= radius2:
                return ("output", sock.name)
        return None

    def hit_param(self, wx: float, wy: float) -> str | None:
        x, y = self.world_position()
        lx = wx - x
        ly = wy - y
        if lx < 0.0 or lx > self.width:
            return None

        row_y = self._params_start_y()
        for name in self.node.params.keys():
            if row_y <= ly <= row_y + self.param_row_height:
                return name
            row_y += self.param_row_height
        return None

    def _draw_socket(
        self,
        renderer,
        transform: SceneTransform,
        socket_name: str,
        socket_type: str,
        *,
        output: bool,
        row_index: int,
    ) -> None:
        x, y = self.world_position()
        cx_w = x + (self.width if output else 0.0)
        cy_w = y + self.title_height + self.socket_row_height * (row_index + 0.5)
        cx, cy = transform.world_to_screen(cx_w, cy_w)
        size = max(4.0, self.socket_size * transform.zoom)
        label_font = max(6.0, self.label_font_size * transform.zoom)
        renderer.draw_rect(
            cx - size / 2.0,
            cy - size / 2.0,
            size,
            size,
            self._socket_colors.get(socket_type, self._socket_colors["any"]),
        )

        if output:
            renderer.draw_text(
                cx - max(8.0, self.width * 0.45 * transform.zoom),
                cy + label_font / 3.0,
                socket_name,
                self.text_color,
                label_font,
            )
        else:
            renderer.draw_text(
                cx + 8.0,
                cy + label_font / 3.0,
                socket_name,
                self.text_color,
                label_font,
            )

    def paint(self, renderer, transform: SceneTransform) -> None:
        wx, wy, ww, wh = self.world_bounds()
        sx, sy = transform.world_to_screen(wx, wy)
        sw = ww * transform.zoom
        sh = wh * transform.zoom
        th = self.title_height * transform.zoom
        title_font = max(7.0, self.font_size * transform.zoom)
        param_font = max(6.0, (self.label_font_size - 1.0) * transform.zoom)

        renderer.draw_rect(sx, sy, sw, sh, self.fill_color)
        renderer.draw_rect(sx, sy, sw, th, self.title_bg_color)
        border = self.border_selected_color if self.selected else self.border_color
        renderer.draw_rect_outline(sx, sy, sw, sh, border, self.border_width)
        renderer.draw_text(
            sx + 8.0,
            sy + min(th - 4.0, title_font + 6.0),
            self.label,
            self.text_color,
            title_font,
        )

        for i, sock in enumerate(self.node.inputs):
            self._draw_socket(
                renderer,
                transform,
                sock.name,
                sock.socket_type,
                output=False,
                row_index=i,
            )
        for i, sock in enumerate(self.node.outputs):
            self._draw_socket(
                renderer,
                transform,
                sock.name,
                sock.socket_type,
                output=True,
                row_index=i,
            )

        if self.draw_param_names or self.draw_param_values:
            param_specs = self.node.data.get("param_specs", {})
            if not isinstance(param_specs, dict):
                param_specs = {}
            row_start_s = transform.world_to_screen(wx, wy + self._params_start_y())[1]
            row_h_s = self.param_row_height * transform.zoom
            for i, (name, value) in enumerate(self.node.params.items()):
                row_y_s = row_start_s + i * row_h_s
                text_y = row_y_s + row_h_s * 0.5 + param_font * 0.34
                label = name
                spec = param_specs.get(name)
                if isinstance(spec, dict):
                    label = str(spec.get("label", name))
                if self.draw_param_names:
                    renderer.draw_text(
                        sx + 8.0,
                        text_y,
                        label,
                        self.param_label_color,
                        param_font,
                    )
                if self.draw_param_values:
                    renderer.draw_text(
                        sx + sw * 0.56,
                        text_y,
                        str(value),
                        self.param_value_color,
                        param_font,
                    )


class EdgeItem(GraphicsItem):
    """Visual line between two node items."""

    def __init__(
        self,
        src_item: NodeItem,
        dst_item: NodeItem,
        src_socket_name: str,
        dst_socket_name: str,
    ) -> None:
        super().__init__()
        self.src_item = src_item
        self.dst_item = dst_item
        self.src_socket_name = src_socket_name
        self.dst_socket_name = dst_socket_name
        self.selectable = True
        self.draggable = False
        self.line_color = (0.75, 0.78, 0.85, 0.95)
        self.selected_line_color = (1.0, 0.85, 0.32, 1.0)
        self.z_index = -10

    def hit_test(self, wx: float, wy: float) -> GraphicsItem | None:
        if not self.visible or not self.enabled:
            return None

        src_pos = self.src_item.socket_world_pos(self.src_socket_name, output=True)
        dst_pos = self.dst_item.socket_world_pos(self.dst_socket_name, output=False)
        if src_pos is None or dst_pos is None:
            return None

        sx, sy = src_pos
        dx, dy = dst_pos
        points = _bezier_points(sx, sy, dx, dy)
        tolerance = 10.0
        tol2 = tolerance * tolerance

        for i in range(len(points) - 1):
            ax, ay = points[i]
            bx, by = points[i + 1]
            if _distance_sq_point_segment(wx, wy, ax, ay, bx, by) <= tol2:
                return self
        return None

    def paint(self, renderer, transform: SceneTransform) -> None:
        src_pos = self.src_item.socket_world_pos(self.src_socket_name, output=True)
        dst_pos = self.dst_item.socket_world_pos(self.dst_socket_name, output=False)
        if src_pos is None or dst_pos is None:
            return

        sx_w, sy_w = src_pos
        dx_w, dy_w = dst_pos

        sx, sy = transform.world_to_screen(sx_w, sy_w)
        dx, dy = transform.world_to_screen(dx_w, dy_w)
        if self.selected:
            _draw_bezier_connection(
                renderer,
                sx,
                sy,
                dx,
                dy,
                (1.0, 0.93, 0.55, 0.45),
                base_thickness=4.2,
            )
            _draw_bezier_connection(
                renderer,
                sx,
                sy,
                dx,
                dy,
                self.selected_line_color,
                base_thickness=2.6,
            )
        else:
            _draw_bezier_connection(renderer, sx, sy, dx, dy, self.line_color, base_thickness=1.8)


class NodeGraphSceneAdapter:
    """Builds GraphicsScene from Graph model."""

    def __init__(self, graph: Graph) -> None:
        self.graph = graph
        self.scene = GraphicsScene()
        self.node_items: dict[str, NodeItem] = {}
        self.group_items: dict[str, GroupItem] = {}
        self.edge_items: dict[str, EdgeItem] = {}

    def rebuild(self) -> None:
        self.scene.clear()
        self.node_items.clear()
        self.group_items.clear()
        self.edge_items.clear()

        for g in self.graph.groups.values():
            item = GroupItem(g.title)
            item.x = g.x
            item.y = g.y
            item.width = g.width
            item.height = g.height
            item.z_index = -20
            item.data["group_id"] = g.id
            self.scene.add_item(item)
            self.group_items[g.id] = item

        for n in self.graph.nodes.values():
            item = NodeItem(n.id, n.title)
            item.x = n.x
            item.y = n.y
            item.width = n.width
            item.data["node"] = n
            item.data["node_id"] = n.id
            explicit_size = bool(n.data.get("explicit_size", False))
            if explicit_size:
                item.height = n.height
            else:
                item.height = max(n.height, item.content_min_height())
            item.configure_palette(
                str(n.data.get("node_type", n.kind)),
                str(n.data.get("graph_type", n.title)),
            )
            self.scene.add_item(item)
            self.node_items[n.id] = item

        for e in self.graph.edges.values():
            src = self.node_items.get(e.src_node_id)
            dst = self.node_items.get(e.dst_node_id)
            if src is None or dst is None:
                continue
            edge_item = EdgeItem(src, dst, e.src_socket, e.dst_socket)
            src_node = self.graph.nodes.get(e.src_node_id)
            if src_node is not None:
                for socket in src_node.outputs:
                    if socket.name == e.src_socket:
                        edge_item.line_color = src._socket_colors.get(
                            socket.socket_type,
                            src._socket_colors["any"],
                        )
                        break
            edge_item.data["edge_id"] = e.id
            self.scene.add_item(edge_item)
            self.edge_items[e.id] = edge_item

    def apply_item_positions_to_model(self) -> None:
        for node_id, item in self.node_items.items():
            node = self.graph.nodes.get(node_id)
            if node is None:
                continue
            node.x = item.x
            node.y = item.y

        for group_id, item in self.group_items.items():
            group = self.graph.groups.get(group_id)
            if group is None:
                continue
            group.x = item.x
            group.y = item.y


class NodeGraphView(SceneView):
    """Scene view bound to Graph model."""

    def __init__(self, graph: Graph) -> None:
        self.controller = GraphController(graph)
        self.adapter = NodeGraphSceneAdapter(graph)
        self.adapter.rebuild()
        super().__init__(self.adapter.scene)
        self.background_color = (0.09, 0.10, 0.12, 1.0)
        self.grid_color = (0.15, 0.16, 0.20, 1.0)
        self.grid_axis_color = (0.24, 0.27, 0.34, 1.0)
        self._pending_connection: tuple[NodeItem, str, bool] | None = None
        self._pending_mouse_world: tuple[float, float] | None = None
        # Called on RMB: fn(world_x, world_y) -> list[MenuItem]
        self.menu_items_provider = None
        # Keep inline value edits optional; external inspectors can disable this.
        self.inline_param_editing = True
        self.use_param_widgets = False
        self.param_widget_zoom_threshold = 0.72
        self.param_widget_row_height = 24.0
        self.param_widget_height = 20.0
        self.param_widget_min_width = 64.0
        self.on_param_changed = None

    def refresh(self) -> None:
        self.adapter.rebuild()
        self.scene = self.adapter.scene
        self._rebuild_param_widgets()

    def set_graph(self, graph: Graph) -> None:
        self.controller = GraphController(graph)
        self.adapter = NodeGraphSceneAdapter(graph)
        self.adapter.rebuild()
        self.scene = self.adapter.scene
        self._pending_connection = None
        self._pending_mouse_world = None
        self._rebuild_param_widgets()

    def _param_spec(self, node: Node, name: str, value: Any) -> dict[str, Any]:
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

    def _set_param(self, node: Node, name: str, value: Any) -> None:
        node.params[name] = value
        if self.on_param_changed is not None:
            self.on_param_changed(node, name, value)

    def _make_bool_param_widget(self, node: Node, name: str, value: Any) -> Checkbox:
        widget = Checkbox()
        widget.checked = bool(value)
        widget.on_changed = lambda checked, n=node, k=name: self._set_param(n, k, checked)
        return widget

    def _make_enum_param_widget(self, node: Node, name: str, value: Any, spec: dict[str, Any]) -> ComboBox:
        widget = ComboBox()
        items = spec.get("items", [])
        if not isinstance(items, list):
            items = []
        values = []
        for item in items:
            if isinstance(item, dict):
                item_value = item.get("value")
                item_label = item.get("label")
                value_text = str(item_value) if item_value is not None else ""
                label_text = str(item_label) if item_label is not None else value_text
                values.append(value_text)
                widget.add_item(label_text)
            else:
                value_text = str(item)
                values.append(value_text)
                widget.add_item(value_text)

        current = str(value)
        selected_index = -1
        for i, item_value in enumerate(values):
            if item_value == current:
                selected_index = i
                break
        if selected_index < 0 and current:
            widget.add_item(current)
            values.append(current)
            selected_index = len(widget.items) - 1
        widget.selected_index = selected_index
        def on_changed(idx: int, text: str, n=node, k=name) -> None:
            if 0 <= idx < len(values):
                self._set_param(n, k, values[idx])
            else:
                self._set_param(n, k, text)
        widget.on_changed = on_changed
        return widget

    def _make_int_param_widget(self, node: Node, name: str, value: Any, spec: dict[str, Any]) -> SpinBox:
        widget = SpinBox()
        widget.decimals = 0
        widget.step = float(spec.get("step", 1))
        widget.min_value = float(spec.get("min", -1e9))
        widget.max_value = float(spec.get("max", 1e9))
        widget.value = float(value)
        widget.on_changed = lambda v, n=node, k=name: self._set_param(n, k, int(round(v)))
        return widget

    def _make_float_param_widget(self, node: Node, name: str, value: Any, spec: dict[str, Any]) -> SpinBox:
        widget = SpinBox()
        widget.decimals = int(spec.get("decimals", 2))
        widget.step = float(spec.get("step", 0.1))
        widget.min_value = float(spec.get("min", -1e9))
        widget.max_value = float(spec.get("max", 1e9))
        widget.value = float(value)
        widget.on_changed = lambda v, n=node, k=name: self._set_param(n, k, float(v))
        return widget

    def _make_text_param_widget(self, node: Node, name: str, value: Any) -> TextInput:
        widget = TextInput()
        widget.text = str(value)
        widget.on_changed = lambda text, n=node, k=name: self._set_param(n, k, text)
        return widget

    def _make_param_widget(self, node: Node, name: str, value: Any, spec: dict[str, Any]):
        kind = str(spec.get("kind", "string")).lower()
        if kind == "bool":
            return self._make_bool_param_widget(node, name, value)
        if kind == "enum":
            return self._make_enum_param_widget(node, name, value, spec)
        if kind == "int":
            return self._make_int_param_widget(node, name, value, spec)
        if kind == "float":
            return self._make_float_param_widget(node, name, value, spec)
        return self._make_text_param_widget(node, name, value)

    def _rebuild_param_widgets(self) -> None:
        for node_id, item in self.adapter.node_items.items():
            node = self.adapter.graph.nodes.get(node_id)
            if node is None:
                continue
            item.children.clear()
            if not self.use_param_widgets:
                continue

            item.param_row_height = self.param_widget_row_height
            item.draw_param_names = True
            item.draw_param_values = False
            item.height = max(item.height, item.content_min_height())

            row_y = item._params_start_y()
            for row_index, (name, value) in enumerate(node.params.items()):
                spec = self._param_spec(node, name, value)
                editor = self._make_param_widget(node, name, value, spec)
                widget_item = GraphicsWidgetItem(editor)
                widget_item.x = item.width * 0.52
                widget_item.y = row_y + (item.param_row_height - self.param_widget_height) * 0.5
                widget_item.width = max(self.param_widget_min_width, item.width * 0.46 - 8.0)
                widget_item.height = self.param_widget_height
                widget_item.z_index = 10.0
                widget_item.data["row_index"] = row_index
                item.add_child(widget_item)
                row_y += item.param_row_height

    def _update_param_widget_lod(self) -> None:
        if not self.use_param_widgets:
            return
        show_editors = self.zoom >= self.param_widget_zoom_threshold
        for item in self.adapter.node_items.values():
            item.draw_param_values = not show_editors
            for child_item in item.children:
                if isinstance(child_item, GraphicsWidgetItem):
                    child_item.visible = show_editors
                    child_item.enabled = show_editors

    def _draw_pending_connection(self, renderer) -> None:
        if self._pending_connection is None or self._pending_mouse_world is None:
            return
        item, socket_name, is_output = self._pending_connection
        pos = item.socket_world_pos(socket_name, output=is_output)
        if pos is None:
            return

        sx_w, sy_w = pos
        ex_w, ey_w = self._pending_mouse_world
        transform = self._make_transform()
        sx, sy = transform.world_to_screen(sx_w, sy_w)
        ex, ey = transform.world_to_screen(ex_w, ey_w)
        color = (0.9, 0.86, 0.55, 1.0)
        _draw_bezier_connection(renderer, sx, sy, ex, ey, color, base_thickness=2.0)

    def render(self, renderer) -> None:
        self._update_param_widget_lod()
        super().render(renderer)
        renderer.begin_clip(self.x, self.y, self.width, self.height)
        self._draw_pending_connection(renderer)
        renderer.end_clip()

    def on_mouse_down(self, event) -> bool:
        if event.button == MouseButton.RIGHT:
            if self._ui is not None and self.menu_items_provider is not None:
                wx, wy = self.screen_to_world(event.x, event.y)
                items = self.menu_items_provider(wx, wy)
                if items:
                    menu = Menu()
                    menu.items = items
                    menu.show(self._ui, event.x, event.y)
                    return True
            return super().on_mouse_down(event)

        if event.button != MouseButton.LEFT:
            return super().on_mouse_down(event)

        wx, wy = self.screen_to_world(event.x, event.y)
        hit = self.scene.hit_test(wx, wy)
        if isinstance(hit, NodeItem):
            param_hit = hit.hit_param(wx, wy)
            if self.inline_param_editing and param_hit is not None and self._ui is not None:
                current = hit.node.params.get(param_hit)

                if isinstance(current, bool):
                    self._set_param(hit.node, param_hit, not current)
                    return True

                def _apply_text(result: str | None, node=hit.node, key=param_hit, old=current) -> None:
                    if result is None:
                        return
                    if isinstance(old, int):
                        try:
                            self._set_param(node, key, int(result))
                        except ValueError:
                            logging.getLogger(__name__).warning("Failed to parse int for param %r: %r", key, result)
                            return
                    elif isinstance(old, float):
                        try:
                            self._set_param(node, key, float(result))
                        except ValueError:
                            logging.getLogger(__name__).warning("Failed to parse float for param %r: %r", key, result)
                            return
                    else:
                        self._set_param(node, key, result)

                show_input_dialog(
                    self._ui,
                    title=f"Edit {param_hit}",
                    message=f"Set value for '{param_hit}'",
                    default=str(current),
                    on_result=_apply_text,
                )
                return True

            socket_hit = hit.hit_socket(wx, wy)
            if socket_hit is not None:
                direction, socket_name = socket_hit
                is_output = direction == "output"
                self._pending_connection = (hit, socket_name, is_output)
                self._pending_mouse_world = (wx, wy)
                self.scene.set_selected(hit)
                return True
        return super().on_mouse_down(event)

    def on_mouse_move(self, event) -> None:
        if self._pending_connection is not None:
            self._pending_mouse_world = self.screen_to_world(event.x, event.y)
            return
        super().on_mouse_move(event)

    def on_mouse_up(self, event) -> None:
        if event.button == MouseButton.LEFT and self._pending_connection is not None:
            wx, wy = self.screen_to_world(event.x, event.y)
            start_item, start_socket, start_is_output = self._pending_connection
            self._pending_connection = None
            self._pending_mouse_world = None

            hit = self.scene.hit_test(wx, wy)
            if isinstance(hit, NodeItem):
                socket_hit = hit.hit_socket(wx, wy)
                if socket_hit is not None:
                    direction, socket_name = socket_hit
                    end_is_output = direction == "output"
                    if start_item is not hit and start_is_output != end_is_output:
                        if start_is_output:
                            src_node_id = start_item.node_id
                            src_socket = start_socket
                            dst_node_id = hit.node_id
                            dst_socket = socket_name
                        else:
                            src_node_id = hit.node_id
                            src_socket = socket_name
                            dst_node_id = start_item.node_id
                            dst_socket = start_socket

                        result = self.controller.connect(
                            src_node_id,
                            src_socket,
                            dst_node_id,
                            dst_socket,
                        )
                        if result.ok:
                            self.refresh()
            return

        super().on_mouse_up(event)
        self.adapter.apply_item_positions_to_model()

    def on_key_down(self, event) -> bool:
        deleted = False
        if event.key == Key.DELETE:
            # Process selected items from top-level scene.
            selected = self.scene.selected_items[:]
            for item in selected:
                edge_id = item.data.get("edge_id")
                node_id = item.data.get("node_id")
                group_id = item.data.get("group_id")
                if edge_id is not None:
                    deleted = self.controller.remove_edge(edge_id) or deleted
                elif node_id is not None:
                    deleted = self.controller.remove_node(node_id) or deleted
                elif group_id is not None:
                    deleted = self.controller.remove_group(group_id) or deleted
            if deleted:
                self.refresh()
                return True
        return super().on_key_down(event)
