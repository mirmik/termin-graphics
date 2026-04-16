"""2D Plot widget for tcgui - thin adapter over the C++ PlotEngine2D."""

from __future__ import annotations

from tcgui.widgets.widget import Widget
from tcgui.widgets.events import MouseEvent, MouseWheelEvent

from tcplot._tcplot_native import PlotEngine2D


class Plot2D(Widget):
    """tcgui Widget that hosts a C++ ``PlotEngine2D``.

    The engine owns all plot state, rendering and interaction logic —
    this class only translates tcgui's widget lifecycle and events
    into engine calls, and relays the engine's public API so the demo
    scripts keep their ``plot.plot(x, y)`` idiom.
    """

    def __init__(self):
        super().__init__()
        self.engine = PlotEngine2D()

    # -- Proxied public API --

    @property
    def data(self):
        return self.engine.data

    def plot(self, x, y, *, color=None, thickness=1.5, label=""):
        self.engine.plot(x, y, color=color, thickness=thickness, label=label)

    def scatter(self, x, y, *, color=None, size=4.0, label=""):
        self.engine.scatter(x, y, color=color, size=size, label=label)

    def clear(self):
        self.engine.clear()

    def fit(self):
        self.engine.fit()

    def set_view(self, x_min: float, x_max: float, y_min: float, y_max: float):
        self.engine.set_view(x_min, x_max, y_min, y_max)

    # -- Widget hooks --

    def render(self, renderer):
        self.engine.set_viewport(self.x, self.y, self.width, self.height)
        holder = renderer.holder
        if holder is None:
            return
        self.engine.render(holder.context, renderer.font)

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
