"""termin-nodegraph interactive demo with scene-native inline node editors."""

from __future__ import annotations

import ctypes
from typing import Any

import sdl2
from OpenGL.GL import GL_MULTISAMPLE, glEnable
from sdl2 import video

from tcbase import Key, Mods, MouseButton
from tcgui.scene import GraphicsWidgetItem
from tcgui.widgets import Checkbox, ComboBox, SpinBox, TextInput
from tcgui.widgets.ui import UI
from tcgui.widgets.units import pct
from tcgui.widgets.vstack import VStack
from tgfx import OpenGLGraphicsBackend

from tcnodegraph import Graph, GraphController, NodeGraphView


def create_window(title: str, width: int, height: int):
    if sdl2.SDL_Init(sdl2.SDL_INIT_VIDEO) != 0:
        raise RuntimeError(f"SDL_Init failed: {sdl2.SDL_GetError()}")

    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MAJOR_VERSION, 3)
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_MINOR_VERSION, 3)
    video.SDL_GL_SetAttribute(video.SDL_GL_CONTEXT_PROFILE_MASK, video.SDL_GL_CONTEXT_PROFILE_CORE)
    video.SDL_GL_SetAttribute(video.SDL_GL_DOUBLEBUFFER, 1)
    video.SDL_GL_SetAttribute(video.SDL_GL_DEPTH_SIZE, 24)
    video.SDL_GL_SetAttribute(video.SDL_GL_MULTISAMPLEBUFFERS, 1)
    video.SDL_GL_SetAttribute(video.SDL_GL_MULTISAMPLESAMPLES, 4)

    flags = video.SDL_WINDOW_OPENGL | video.SDL_WINDOW_RESIZABLE | video.SDL_WINDOW_SHOWN
    window = video.SDL_CreateWindow(
        title.encode("utf-8"),
        video.SDL_WINDOWPOS_CENTERED,
        video.SDL_WINDOWPOS_CENTERED,
        width,
        height,
        flags,
    )
    if not window:
        raise RuntimeError(f"SDL_CreateWindow failed: {sdl2.SDL_GetError()}")

    gl_ctx = video.SDL_GL_CreateContext(window)
    if not gl_ctx:
        video.SDL_DestroyWindow(window)
        raise RuntimeError(f"SDL_GL_CreateContext failed: {sdl2.SDL_GetError()}")

    video.SDL_GL_MakeCurrent(window, gl_ctx)
    video.SDL_GL_SetSwapInterval(1)
    return window, gl_ctx


def get_drawable_size(window) -> tuple[int, int]:
    w, h = ctypes.c_int(), ctypes.c_int()
    video.SDL_GL_GetDrawableSize(window, ctypes.byref(w), ctypes.byref(h))
    return w.value, h.value


_KEY_MAP = {
    sdl2.SDL_SCANCODE_BACKSPACE: Key.BACKSPACE,
    sdl2.SDL_SCANCODE_DELETE: Key.DELETE,
    sdl2.SDL_SCANCODE_LEFT: Key.LEFT,
    sdl2.SDL_SCANCODE_RIGHT: Key.RIGHT,
    sdl2.SDL_SCANCODE_UP: Key.UP,
    sdl2.SDL_SCANCODE_DOWN: Key.DOWN,
    sdl2.SDL_SCANCODE_HOME: Key.HOME,
    sdl2.SDL_SCANCODE_END: Key.END,
    sdl2.SDL_SCANCODE_RETURN: Key.ENTER,
    sdl2.SDL_SCANCODE_ESCAPE: Key.ESCAPE,
    sdl2.SDL_SCANCODE_TAB: Key.TAB,
    sdl2.SDL_SCANCODE_SPACE: Key.SPACE,
}


def translate_key(scancode: int):
    if scancode in _KEY_MAP:
        return _KEY_MAP[scancode]
    keycode = sdl2.SDL_GetKeyFromScancode(scancode)
    if 0 <= keycode < 128:
        try:
            return Key(keycode)
        except ValueError:
            pass
    return Key.UNKNOWN


def translate_mods(sdl_mods: int) -> int:
    result = 0
    if sdl_mods & (sdl2.KMOD_LSHIFT | sdl2.KMOD_RSHIFT):
        result |= Mods.SHIFT.value
    if sdl_mods & (sdl2.KMOD_LCTRL | sdl2.KMOD_RCTRL):
        result |= Mods.CTRL.value
    if sdl_mods & (sdl2.KMOD_LALT | sdl2.KMOD_RALT):
        result |= Mods.ALT.value
    return result


_SDL_BUTTON_MAP = {1: MouseButton.LEFT, 2: MouseButton.MIDDLE, 3: MouseButton.RIGHT}


def translate_button(sdl_button: int) -> MouseButton:
    return _SDL_BUTTON_MAP.get(sdl_button, MouseButton.LEFT)


def make_demo_graph() -> Graph:
    g = Graph()
    c = GraphController(g)

    a = c.create_node("pass", title="ColorPass", x=-260, y=-70)
    a.params.update({
        "enabled": True,
        "samples": 4,
        "exposure": 1.1,
        "quality": "High",
        "label": "Main Color",
    })
    a.data["param_specs"] = {
        "enabled": {"kind": "bool", "label": "Enabled"},
        "samples": {"kind": "int", "label": "MSAA Samples", "min": 1, "max": 16, "step": 1},
        "exposure": {"kind": "float", "label": "Exposure", "min": 0.05, "max": 8.0, "step": 0.05, "decimals": 2},
        "quality": {"kind": "enum", "label": "Quality", "items": ["Low", "Medium", "High", "Ultra"]},
        "label": {"kind": "string", "label": "Debug Label"},
    }

    b = c.create_node("pass", title="BloomPass", x=40, y=-40)
    b.params.update({
        "enabled": True,
        "threshold": 1.25,
        "iterations": 5,
        "mode": "Karis",
    })
    b.data["param_specs"] = {
        "enabled": {"kind": "bool", "label": "Enabled"},
        "threshold": {"kind": "float", "label": "Threshold", "min": 0.0, "max": 4.0, "step": 0.05, "decimals": 2},
        "iterations": {"kind": "int", "label": "Iterations", "min": 1, "max": 16, "step": 1},
        "mode": {"kind": "enum", "label": "Mode", "items": ["Legacy", "Karis", "Physically Based"]},
    }

    d = c.create_node("pass", title="Present", x=340, y=-10)
    d.params.update({"vsync": True, "gamma": 2.2, "output": "sRGB"})
    d.data["param_specs"] = {
        "vsync": {"kind": "bool", "label": "VSync"},
        "gamma": {"kind": "float", "label": "Gamma", "min": 1.0, "max": 3.0, "step": 0.01, "decimals": 2},
        "output": {"kind": "enum", "label": "Output", "items": ["Linear", "sRGB", "HDR10"]},
    }

    r = c.create_node("resource", title="SceneColor", x=-280, y=-240)
    r.params.update({"format": "RGBA16F", "width": 1920, "height": 1080})
    r.data["param_specs"] = {
        "format": {"kind": "enum", "label": "Format", "items": ["RGBA8", "RGBA16F", "RGBA32F"]},
        "width": {"kind": "int", "label": "Width", "min": 64, "max": 8192, "step": 1},
        "height": {"kind": "int", "label": "Height", "min": 64, "max": 8192, "step": 1},
    }

    c.add_output_socket(r.id, "fbo", "fbo")
    c.add_input_socket(a.id, "input_res", "fbo")
    c.add_output_socket(a.id, "output_res", "fbo")
    c.add_input_socket(b.id, "input_res", "fbo")
    c.add_output_socket(b.id, "output_res", "fbo")
    c.add_input_socket(d.id, "input_res", "fbo")

    c.connect(r.id, "fbo", a.id, "input_res")
    c.connect(a.id, "output_res", b.id, "input_res")
    c.connect(b.id, "output_res", d.id, "input_res")
    c.add_group("Main Viewport", -340, -290, 760, 420)
    return g


class InlineEditorNodeGraphView(NodeGraphView):
    """NodeGraphView that mounts editable controls directly into node rows."""

    def __init__(self, graph: Graph) -> None:
        super().__init__(graph)
        self.inline_param_editing = False
        self.param_editor_zoom_threshold = 0.72
        self.param_row_height = 24.0
        self.param_editor_height = 20.0
        self.param_editor_min_width = 56.0
        self._rebuild_param_widgets()

    def _param_spec(self, node, name: str, value: Any) -> dict[str, Any]:
        specs = node.data.get("param_specs", {})
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

    def _set_param(self, node, name: str, value: Any) -> None:
        node.params[name] = value

    def _apply_widget_zoom_style(self, gw: GraphicsWidgetItem) -> None:
        widget = gw.widget
        base = gw.data.get("base_style")
        if base is None:
            base = {}
            for attr in ("font_size", "padding", "button_width", "box_size", "spacing", "border_width"):
                if hasattr(widget, attr):
                    base[attr] = getattr(widget, attr)
            gw.data["base_style"] = base

        scale = max(0.2, self.zoom)
        for attr, base_value in base.items():
            setattr(widget, attr, base_value * scale)

    def _make_editor_widget(self, node, name: str, value: Any, spec: dict[str, Any]):
        kind = str(spec.get("kind", "string")).lower()

        if kind == "bool":
            w = Checkbox()
            w.checked = bool(value)
            w.on_changed = lambda checked, n=node, k=name: self._set_param(n, k, checked)
            return w

        if kind == "enum":
            w = ComboBox()
            for item in spec.get("items", []):
                w.add_item(str(item))
            current = str(value)
            idx = next((i for i, item in enumerate(w.items) if item == current), -1)
            if idx < 0 and current:
                w.add_item(current)
                idx = len(w.items) - 1
            w.selected_index = idx
            w.on_changed = lambda _idx, text, n=node, k=name: self._set_param(n, k, text)
            return w

        if kind == "int":
            w = SpinBox()
            w.decimals = 0
            w.step = float(spec.get("step", 1))
            w.min_value = float(spec.get("min", -1e9))
            w.max_value = float(spec.get("max", 1e9))
            w.value = float(value)
            w.on_changed = lambda v, n=node, k=name: self._set_param(n, k, int(round(v)))
            return w

        if kind == "float":
            w = SpinBox()
            w.decimals = int(spec.get("decimals", 2))
            w.step = float(spec.get("step", 0.1))
            w.min_value = float(spec.get("min", -1e9))
            w.max_value = float(spec.get("max", 1e9))
            w.value = float(value)
            w.on_changed = lambda v, n=node, k=name: self._set_param(n, k, float(v))
            return w

        w = TextInput()
        w.text = str(value)
        w.on_changed = lambda text, n=node, k=name: self._set_param(n, k, text)
        return w

    def _rebuild_param_widgets(self) -> None:
        for node_id, item in self.adapter.node_items.items():
            node = self.adapter.graph.nodes.get(node_id)
            if node is None:
                continue
            item.param_row_height = self.param_row_height
            item.draw_param_names = True
            item.draw_param_values = False
            item.children.clear()
            item.height = max(item.height, item.content_min_height())

            row_y = item._params_start_y()
            for row_index, (name, value) in enumerate(node.params.items()):
                spec = self._param_spec(node, name, value)
                editor = self._make_editor_widget(node, name, value, spec)
                gw = GraphicsWidgetItem(editor)
                gw.x = item.width * 0.52
                gw.y = row_y + (item.param_row_height - self.param_editor_height) * 0.5
                gw.width = max(self.param_editor_min_width, item.width * 0.46 - 8.0)
                gw.height = self.param_editor_height
                gw.z_index = 10.0
                gw.data["row_index"] = row_index
                self._apply_widget_zoom_style(gw)
                item.add_child(gw)
                row_y += item.param_row_height

    def _update_editor_lod(self) -> None:
        show_editors = self.zoom >= self.param_editor_zoom_threshold
        for item in self.adapter.node_items.values():
            item.draw_param_values = not show_editors
            for child in item.children:
                if isinstance(child, GraphicsWidgetItem):
                    child.visible = show_editors
                    child.enabled = show_editors
                    self._apply_widget_zoom_style(child)

    def refresh(self) -> None:
        super().refresh()
        self._rebuild_param_widgets()
        self._update_editor_lod()

    def set_graph(self, graph: Graph) -> None:
        super().set_graph(graph)
        self._rebuild_param_widgets()
        self._update_editor_lod()

    def render(self, renderer) -> None:
        self._update_editor_lod()
        super().render(renderer)


def build_ui(graphics) -> UI:
    root = VStack()
    root.preferred_width = pct(100)
    root.preferred_height = pct(100)

    graph = make_demo_graph()
    view = InlineEditorNodeGraphView(graph)
    view.preferred_width = pct(100)
    view.preferred_height = pct(100)
    view.offset_x = 500
    view.offset_y = 330

    root.add_child(view)
    ui = UI(graphics)
    ui.root = root
    return ui


def main():
    window, gl_ctx = create_window("termin-nodegraph demo", 1280, 820)
    try:
        graphics = OpenGLGraphicsBackend.get_instance()
        graphics.ensure_ready()
        glEnable(GL_MULTISAMPLE)
        ui = build_ui(graphics)

        sdl2.SDL_StartTextInput()
        event = sdl2.SDL_Event()
        running = True

        while running:
            while sdl2.SDL_PollEvent(ctypes.byref(event)) != 0:
                t = event.type
                if t == sdl2.SDL_QUIT:
                    running = False
                    break
                elif t == sdl2.SDL_WINDOWEVENT and event.window.event == video.SDL_WINDOWEVENT_CLOSE:
                    running = False
                    break
                elif t == sdl2.SDL_MOUSEMOTION:
                    ui.mouse_move(float(event.motion.x), float(event.motion.y))
                elif t == sdl2.SDL_MOUSEBUTTONDOWN:
                    ui.mouse_down(
                        float(event.button.x),
                        float(event.button.y),
                        translate_button(event.button.button),
                        translate_mods(sdl2.SDL_GetModState()),
                    )
                elif t == sdl2.SDL_MOUSEBUTTONUP:
                    ui.mouse_up(
                        float(event.button.x),
                        float(event.button.y),
                        translate_button(event.button.button),
                        translate_mods(sdl2.SDL_GetModState()),
                    )
                elif t == sdl2.SDL_MOUSEWHEEL:
                    mx, my = ctypes.c_int(), ctypes.c_int()
                    sdl2.SDL_GetMouseState(ctypes.byref(mx), ctypes.byref(my))
                    ui.mouse_wheel(float(event.wheel.x), float(event.wheel.y), float(mx.value), float(my.value))
                elif t == sdl2.SDL_KEYDOWN:
                    key = translate_key(event.key.keysym.scancode)
                    if key == Key.ESCAPE:
                        running = False
                        break
                    ui.key_down(key, translate_mods(sdl2.SDL_GetModState()))
                elif t == sdl2.SDL_TEXTINPUT:
                    ui.text_input(event.text.text.decode("utf-8"))

            if not running:
                break

            ui.process_deferred()

            w, h = get_drawable_size(window)
            graphics.bind_framebuffer(None)
            graphics.set_viewport(0, 0, w, h)
            graphics.clear_color_depth(0.08, 0.08, 0.10, 1.0)
            ui.render(w, h)
            video.SDL_GL_SwapWindow(window)
    finally:
        video.SDL_GL_DeleteContext(gl_ctx)
        video.SDL_DestroyWindow(window)
        sdl2.SDL_Quit()


if __name__ == "__main__":
    main()
