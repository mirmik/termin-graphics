# tcplot

`tcplot` — lightweight plotting library поверх `termin-graphics`. Retained
plot annotations используют `termin-visual-scene` как внутреннюю
транзитивную зависимость; приложение по-прежнему линкует только
`tcplot::tcplot`.

Связанные документы:

- [Module Map](../../docs/modules.md#tcplot)
- [termin-graphics](../../termin-graphics/docs/index.md)
- [termin-gui](../../termin-gui/docs/index.md)

## Основные области

- Public C++ headers в `include/tcplot/`.
- Implementation в `src/`.
- Python bindings в `python/bindings/`.
- Python package в `python/tcplot/`.
- Examples в `examples/`.

## C++ API

Минимальный consumer подключает один target:

```cmake
find_package(tcplot CONFIG REQUIRED)
target_link_libraries(my_plot PRIVATE tcplot::tcplot)
```

`PlotEngine2D::plot_frame()` возвращает immutable `PlotFrame2D`: detached
snapshot viewport, plot area, data range, clip и прямого/обратного
data-to-pixel преобразования. Снимок не меняется после последующих pan, zoom,
resize или синхронизации общей оси X.

`PlotAnnotationLayer2D` принадлежит plot engine и хранит semantic annotations
через generation handles. Одна annotation может проецироваться в несколько
visual items с независимыми фазами `Underlay`, `Overlay`, `Chrome` и clipping
к plot area либо viewport. Поддерживаются data anchors, series-point
references, axes fractions и viewport pixels.

Готовый `PlotDataMarker2D` создаётся через `create_data_marker()`. Он объединяет
data-anchored точку, plot-clipped leader, pixel-sized callout и text, hover,
captured drag с обновлением data position, optional snapping и semantic
действие `close`. Anchor и leader клипуются plot area; callout остаётся
доступным в viewport. Input annotation маршрутизируется перед plot navigation.

## Python API

Python package:

```python
import tcplot
```

Examples запускаются bundled Python с checkout overlay из корня репозитория:

```bash
./run-python.sh tcplot/examples/demo_sin.py
# Windows:
.\run-python.ps1 tcplot\examples\demo_sin.py
```

Перед первым запуском или после изменения состава SDK нужно выполнить
`setup-sdk-python-env.sh` (на Windows — `setup-sdk-python-env.ps1`).

`PlotEngine2D.create_data_marker()` returns a value-only
`PlotAnnotationHandle`. Marker updates, snapshots, destruction, snapping and
semantic action callbacks all validate the complete
`layer_id/index/generation` identity. `take_annotation_action()` provides the
same actions as a polling queue for hosts that do not use callbacks. Python
callback exceptions are logged at the native boundary and propagated to the
caller.

`annotation_anchor_pixel()` exposes the current projected anchor for overlay
composition and synthetic input without duplicating plot margin or DPI math.
No Python annotation or graphic-item wrapper owns the engine, layer or native
item.
