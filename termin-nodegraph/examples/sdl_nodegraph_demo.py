"""termin-nodegraph interactive demo with scene-native inline node editors."""

from __future__ import annotations

import ctypes

import sdl2
from sdl2 import video

from tcbase import Key, Mods, MouseButton
from tcgui.widgets.ui import UI
from tcgui.widgets.units import pct
from tcgui.widgets.vstack import VStack
from termin.display._platform_native import SDLBackendWindow
from tgfx import Tgfx2Context

from tcnodegraph import Graph, GraphController, NodeGraphView


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


def get_event_window_id(event):
    etype = event.type
    if etype == sdl2.SDL_MOUSEMOTION:
        return event.motion.windowID
    if etype in (sdl2.SDL_MOUSEBUTTONDOWN, sdl2.SDL_MOUSEBUTTONUP):
        return event.button.windowID
    if etype == sdl2.SDL_MOUSEWHEEL:
        return event.wheel.windowID
    if etype in (sdl2.SDL_KEYDOWN, sdl2.SDL_KEYUP):
        return event.key.windowID
    if etype == sdl2.SDL_TEXTINPUT:
        return event.text.windowID
    return None


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


def build_ui(graphics) -> UI:
    root = VStack()
    root.preferred_width = pct(100)
    root.preferred_height = pct(100)

    graph = make_demo_graph()
    view = NodeGraphView(graph)
    view.use_param_widgets = True
    view.inline_param_editing = False
    view.refresh()
    view.preferred_width = pct(100)
    view.preferred_height = pct(100)
    view.offset_x = 500
    view.offset_y = 330

    root.add_child(view)
    ui = UI(graphics=graphics)
    ui.root = root
    return ui


def main():
    window = SDLBackendWindow("termin-nodegraph demo", 1280, 820)
    graphics = Tgfx2Context.from_window(window.device_ptr(), window.context_ptr())
    ui = build_ui(graphics)
    main_id = window.window_id()

    try:
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
                    if event.window.windowID == main_id:
                        running = False
                        break
                    continue

                event_window_id = get_event_window_id(event)
                if event_window_id is not None and event_window_id != main_id:
                    continue

                if t == sdl2.SDL_WINDOWEVENT:
                    continue
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

            w, h = window.framebuffer_size()
            tex = ui.render_compose(w, h, background_color=(0.08, 0.08, 0.10, 1.0))
            if tex is not None:
                window.present(tex)
    finally:
        sdl2.SDL_Quit()


if __name__ == "__main__":
    main()
