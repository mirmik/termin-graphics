# tcplot-gui-native

`tcplot-gui-native` is the optional desktop bridge that exposes a ready-made
2D chart as a `termin-gui-native` widget. It keeps the dependency direction
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
