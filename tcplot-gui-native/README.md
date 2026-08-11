# tcplot-gui-native

`tcplot-gui-native` is the optional desktop bridge that exposes ready-made
2D and 3D charts as `termin-gui-native` widgets. It keeps the dependency direction
clean: neither the UI core knows about plotting nor `tcplot` knows about
widgets.

The widget runtime type is `termin.gui.Plot2D`. It owns a `TcVisualScene`, a
plot projection, grid and retained line-series items, then appends that retained
scene directly to the UI draw list. No intermediate render texture or
`SceneView` is involved.

## UiScript

```yaml
- type: termin.gui.Plot2D
  name: telemetry_plot
  title: SERVO COORDINATE
  x_label: time, s
  y_label: angle, deg
  auto_fit: true
  basis: 250
```

UiScript describes placement and presentation. Runtime code resolves the named
widget, adds line series and supplies their data through the C++ `Plot2D` API.
The initial widget is deliberately read-only: it provides automatic fitting,
an explicit view, grid, ticks, labels and line series, but no pan, zoom, legend
or editor for series definitions.

## C++

```cmake
find_package(tcplot_gui_native CONFIG REQUIRED)
target_link_libraries(my_module PRIVATE
    tcplot_gui_native::tcplot_gui_native)
```

```cpp
auto actual = plot.add_line();
plot.set_line_data(actual, time, coordinate);
```

The caller owns telemetry retention policy. `append_line_data()` does not
discard old samples automatically; applications that expose a fixed history
window should trim their buffers or periodically replace the series data.

## Interactive Plot3D widget

`termin.gui.Plot3D` owns a detached `RetainedChart3D`. Before the native UI
render pass, the document painter gives it the active graphics context and
font atlas; it renders a correctly density- and portal-scaled texture and the
widget composites that texture through the standard `Viewport3D`. Dragging
orbits the camera and the mouse wheel zooms it. The bridge does not introduce
a graphics or plotting dependency into either core module.

Python applications normally use the typed adapter:

```python
import numpy as np
from tcplot_gui_native import Plot3D

plot = Plot3D(document)
t = np.linspace(0.0, 8.0 * np.pi, 240)
plot.plot(np.cos(t), np.sin(t), t * 0.05, thickness=2.0)
plot.set_axis_labels("x", "y", "z")
plot.fit_camera()
```

`plot.widget` is an ordinary parentless native widget reference. It can be
added to a widget tree, used as a `SceneView` portal, or transferred into a
node body:

```python
from tcnodegraph import NodeBodyContent, NodeBodyLayout

def body_content(document, node):
    if node.kind != "trajectory":
        return None
    plot = Plot3D(document)
    plot.plot(node.data["x"], node.data["y"], node.data["z"])
    return NodeBodyContent(plot.widget, NodeBodyLayout(height=300.0))
```

Pass that provider as `body_content_provider=` to
`build_native_node_graph_view`. The graph view then owns the widget and
destroys it when the node is removed, the graph is replaced, or the view is
closed. Chart mutations mark paint state dirty; event-driven hosts should also
request a frame through their normal repaint callback.
