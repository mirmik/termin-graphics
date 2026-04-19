"""3D Plot widget for tcgui - thin adapter over the C++ PlotEngine3D."""

from __future__ import annotations

from tcgui.widgets.widget import Widget
from tcgui.widgets.events import MouseEvent, MouseWheelEvent

from tcplot._tcplot_native import PlotEngine3D


class Plot3D(Widget):
    """tcgui Widget that hosts a C++ ``PlotEngine3D``.

    Adds widget-local composition on top of the engine: background
    fill, title overlay (drawn via the renderer's Text2D), and
    toolbar buttons that toggle engine flags (wireframe, marker).
    """

    # Fall back to the first entry of the default palette for the
    # widget background if the palette changes. Matches the pre-port
    # styles.BG_COLOR constant.
    _BG_COLOR = (0.10, 0.10, 0.12, 1.0)
    _LABEL_COLOR = (0.8, 0.8, 0.8, 1.0)

    def __init__(self):
        super().__init__()
        self.engine = PlotEngine3D()

        # Widget overlay styling (engine doesn't know about it).
        self.bg_color = self._BG_COLOR

        # Toolbar buttons — tcgui-specific composition.
        from tcgui.widgets.button import Button
        from tcgui.widgets.units import px

        self._wire_btn = Button()
        self._wire_btn.text = "W"
        self._wire_btn.preferred_width = px(28)
        self._wire_btn.preferred_height = px(28)
        self._wire_btn.on_click = self.engine.toggle_wireframe
        self.add_child(self._wire_btn)

        self._marker_btn = Button()
        self._marker_btn.text = "M"
        self._marker_btn.preferred_width = px(28)
        self._marker_btn.preferred_height = px(28)
        self._marker_btn.on_click = self.engine.toggle_marker_mode
        self.add_child(self._marker_btn)

    # -- Proxied public API --

    @property
    def data(self):
        return self.engine.data

    @property
    def camera(self):
        return self.engine.camera

    @property
    def z_scale(self) -> float:
        return self.engine.z_scale

    @z_scale.setter
    def z_scale(self, value: float) -> None:
        self.engine.z_scale = value

    @property
    def marker_mode(self) -> bool:
        return self.engine.marker_mode

    @property
    def show_wireframe(self) -> bool:
        return self.engine.show_wireframe

    def plot(self, x, y, z, *, color=None, thickness=1.5, label=""):
        self.engine.plot(x, y, z, color=color, thickness=thickness, label=label)

    def scatter(self, x, y, z, *, color=None, size=4.0, label=""):
        self.engine.scatter(x, y, z, color=color, size=size, label=label)

    def surface(self, X, Y, Z, *, color=None, wireframe=False, label=""):
        # Accept 2D numpy arrays; flatten + pass rows/cols to the
        # native engine which speaks flat buffers only.
        import numpy as np
        Xa = np.ascontiguousarray(X, dtype=np.float64)
        Ya = np.ascontiguousarray(Y, dtype=np.float64)
        Za = np.ascontiguousarray(Z, dtype=np.float64)
        rows, cols = Za.shape
        self.engine.surface(
            Xa.ravel(), Ya.ravel(), Za.ravel(),
            rows, cols,
            color=color, wireframe=wireframe, label=label,
        )

    def clear(self):
        self.engine.clear()

    def pick(self, mx: float, my: float):
        self.engine.set_viewport(self.x, self.y, self.width, self.height)
        return self.engine.pick(mx, my)

    # -- Widget hooks --

    def layout(self, x, y, width, height, viewport_w, viewport_h):
        super().layout(x, y, width, height, viewport_w, viewport_h)
        # Position buttons in top-right corner.
        self._wire_btn.layout(x + width - 36, y + 8, 28, 28, viewport_w, viewport_h)
        self._marker_btn.layout(x + width - 68, y + 8, 28, 28, viewport_w, viewport_h)

    def compute_size(self, viewport_w: float, viewport_h: float) -> tuple[float, float]:
        w = self.preferred_width.to_pixels(viewport_w) if self.preferred_width else viewport_w
        h = self.preferred_height.to_pixels(viewport_h) if self.preferred_height else viewport_h
        return (w, h)

    def render(self, renderer):
        if self.width <= 0 or self.height <= 0:
            return

        # Background overlay.
        renderer.draw_rect(self.x, self.y, self.width, self.height, self.bg_color)

        # Viewport + scissor for the 3D scene.
        self.engine.set_viewport(self.x, self.y, self.width, self.height)
        renderer.begin_clip(int(self.x), int(self.y), int(self.width), int(self.height))

        graphics = renderer.graphics
        if graphics is not None:
            self.engine.render(graphics.context, renderer.font)

        renderer.end_clip()

        # Title overlay drawn through renderer's Text2D.
        if self.data.title:
            renderer.draw_text_centered(
                self.x + self.width / 2,
                self.y + 16,
                self.data.title,
                self._LABEL_COLOR,
                14.0,
            )

        # Render child widgets (toolbar buttons).
        for child in self.children:
            if child.visible:
                child.render(renderer)

    def on_mouse_down(self, event: MouseEvent) -> bool:
        self.engine.set_viewport(self.x, self.y, self.width, self.height)
        return self.engine.on_mouse_down(event.x, event.y, event.button)

    def on_mouse_move(self, event: MouseEvent):
        self.engine.set_viewport(self.x, self.y, self.width, self.height)
        self.engine.on_mouse_move(event.x, event.y)

    def on_mouse_up(self, event: MouseEvent):
        self.engine.on_mouse_up(event.x, event.y, event.button)

    def on_mouse_wheel(self, event: MouseWheelEvent) -> bool:
        self.engine.set_viewport(self.x, self.y, self.width, self.height)
        return self.engine.on_mouse_wheel(event.x, event.y, event.dy)
