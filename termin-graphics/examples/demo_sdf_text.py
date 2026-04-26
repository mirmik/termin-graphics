"""SDF text rendering demo.

Shows text at font sizes from 8 to 48 px. Sizes >= 18 use the SDF
path (crisp at large sizes), smaller sizes use the bitmap path.

Run:  python3 examples/demo_sdf_text.py              (OpenGL)
     TERMIN_BACKEND=vulkan python3 examples/demo_sdf_text.py  (Vulkan)
"""

import ctypes
import sdl2

from tcbase import MouseButton
from tcgui.widgets.ui import UI
from tcgui.widgets.label import Label
from tcgui.widgets.vstack import VStack
from tcgui.widgets.theme import current_theme as _t
from termin.display import BackendWindow
from tgfx._tgfx_native import Tgfx2Context


_SDL_BUTTON_MAP = {1: MouseButton.LEFT, 2: MouseButton.MIDDLE, 3: MouseButton.RIGHT}


def make_ui():
    root = VStack()
    root.spacing = 4

    sizes = [8, 10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48]
    for sz in sizes:
        label = Label()
        label.text = f"Size {sz:2d}px {'[SDF]' if sz >= 18 else '[BMP]'}  "
        label.text += "The quick brown fox jumps over the lazy dog."
        label.font_size = float(sz)
        root.add_child(label)

    return root


def main():
    window = BackendWindow("SDF Text Demo", 1000, 700)
    ctx = Tgfx2Context.from_window(window.device_ptr(), window.context_ptr())
    ui = UI(graphics=ctx)
    ui.root = make_ui()

    event = sdl2.SDL_Event()

    def dispatch(ev):
        t = ev.type
        if t == sdl2.SDL_QUIT or (
            t == sdl2.SDL_KEYDOWN
            and ev.key.keysym.scancode == sdl2.SDL_SCANCODE_ESCAPE
        ):
            window.set_should_close(True)
        elif t == sdl2.SDL_MOUSEMOTION:
            ui.mouse_move(float(ev.motion.x), float(ev.motion.y))
        elif t == sdl2.SDL_MOUSEBUTTONDOWN:
            ui.mouse_down(float(ev.button.x), float(ev.button.y),
                          _SDL_BUTTON_MAP.get(ev.button.button, MouseButton.LEFT))
        elif t == sdl2.SDL_MOUSEBUTTONUP:
            ui.mouse_up(float(ev.button.x), float(ev.button.y),
                        _SDL_BUTTON_MAP.get(ev.button.button, MouseButton.LEFT))
        elif t == sdl2.SDL_MOUSEWHEEL:
            mx, my = ctypes.c_int(), ctypes.c_int()
            sdl2.SDL_GetMouseState(ctypes.byref(mx), ctypes.byref(my))
            ui.mouse_wheel(float(ev.wheel.x), float(ev.wheel.y),
                           float(mx.value), float(my.value))

    while not window.should_close():
        if sdl2.SDL_WaitEventTimeout(ctypes.byref(event), 500):
            dispatch(event)
            while sdl2.SDL_PollEvent(ctypes.byref(event)) != 0:
                dispatch(event)

        w, h = window.framebuffer_size()
        if w <= 0 or h <= 0:
            continue
        tex = ui.render_compose(w, h, background_color=_t.bg_primary)
        ui.process_deferred()
        if tex is not None:
            window.present(tex)


if __name__ == "__main__":
    main()
