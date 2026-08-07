# tcplot

`tcplot` — lightweight plotting library поверх `termin-graphics` и
scene-neutral `termin_render_core`. Retained plot annotations используют
`termin-visual-scene` как внутреннюю транзитивную зависимость; приложение
по-прежнему линкует только `tcplot::tcplot`.

Связанные документы:

- [Module Map](../../docs/modules.md#tcplot)
- [C# Retained Chart Composition](../../docs/architecture/2026-07-30-csharp-retained-chart-composition.md)
- [termin-graphics](../../termin-graphics/docs/index.md)
- [termin-gui](../../termin-gui/docs/index.md)

Текущий `PlotEngine2D` пока остаётся native composer, но его grid/layout и
series rendering уже вынесены в публичные retained parts и общие utilities.
Целевая архитектура собирает эти части в общей `TcVisualScene`, чтобы C# владел
композицией и layout-политикой, а тяжёлые series items рисовались нативно.

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

Готовый native UI chart поставляется отдельным необязательным мостом
[`tcplot-gui-native`](../../tcplot-gui-native/README.md). Runtime-тип
`termin.gui.Plot2D` можно положить прямо в `.uiscript`, а данные линий передать
из C++ через имя виджета. Сам `tcplot` при этом не зависит от UI: bridge
композирует projection, grid и retained series в `TcVisualScene` и добавляет
её draw list непосредственно в paint-команду виджета, без `SceneView` и
промежуточной текстуры.

`PlotEngine2D::plot_frame()` возвращает immutable `PlotFrame2D`: detached
snapshot viewport, plot area, data range, clip и прямого/обратного
data-to-pixel преобразования. Снимок не меняется после последующих pan, zoom,
resize или синхронизации общей оси X.

`PlotProjection2D` — thread-confined generation-checked native projection,
принадлежащая одной `TcVisualScene`. Она хранит компактные viewport, plot area,
clip, data range и pixel scale, обновляется транзакционно и выдаёт immutable
snapshot с revision. Будущие retained series/grid items используют один этот
контракт: pan, zoom и resize не требуют переносить проецированные массивы или
draw-команды через языковую границу. Создание и уничтожение projection явные;
projection следует уничтожать до либо сразу после owner scene.

`PlotGridItem2D` — первый самостоятельный retained chart part. Он принадлежит
обычной `TcVisualScene`, хранит копию major tick values и style, а актуальную
геометрию получает из `PlotProjection2D` во время native paint. Tick values за
пределами текущего range не попадают в draw path, поэтому pan/zoom/resize
передают только compact projection update. C API
`tc_plot_grid_item2d_create/set_*/snapshot/copy_ticks` использует scene и
graphic-item handles, проверяет тип, owner scene и stale state.

`PlotLineSeriesItem2D` и `PlotScatterSeriesItem2D` — самостоятельные native
series parts с обычным `TcVisualScene` lifetime. Они хранят data/style на C++
стороне, читают актуальный `PlotProjection2D`, поддерживают native
nearest-point query и добавляют в draw list только маленькую retained-команду.
Line data живёт в persistent VBO с tail upload для append; solid, styled и
colormap варианты рисуются одним draw call. Scatter хранит instance VBO и
рисуется одним instanced draw вместо Canvas-вызова на каждую точку.

C API в `tc_plot_series_item2d.h` принимает только generation handles,
detached arrays и value styles. Через него можно создать, заменить/добавить
data, изменить style/projection, получить snapshot/copy и выполнить nearest
query. Wrong-type, stale и cross-scene операции отклоняются явно.

Общий `tgfx2::DrawRetainedBatch2D` переносит в native renderer эффективные
transform, opacity, viewport и axis-aligned clip без копирования series
geometry. Для retained batches произвольный geometric clip сейчас явно
отклоняется; plot-area rectangle использует быстрый scissor contract.
`PlotEngine2D` вызывает те же `Plot*SeriesGpu2D`, отдельного legacy renderer
больше нет.

`RetainedChart3D` владеет `PlotScene3DRenderItemSource`, доступным C++ consumer
через `plot_scene3d_render_item_source()`. Он публикует immutable surface,
scatter и grid identities в общий `termin_render_core` без `tc_scene`, entity
или component metadata. Каждый item также ссылается на snapshot-owned
`PlotScene3DRenderItemPayload`: item geometry/style переиспользуются через
immutable shared value до смены revision, а camera/axis/light/bounds/labels
копируются для конкретной публикации. Payload не заимствует retained slot и
остаётся читаемым после последующих mutations, slot reuse и уничтожения chart.
Surface payload дополнительно кэширует encoder-ready vertex stream по geometry
и style revisions. Tcplot-owned surface planner выбирает builtin tcplot3d
shader, а encoder загружает stream через общий transient vertex ring и рисует
его через `submit_render_item_draw()`. Даже старый offscreen entry point теперь
планирует и отправляет surface через этот общий контракт; surface slot больше
не содержит `PlotEngine3D`. Scatter/grid bodies, chart framegraph pass и
отдельный world-text/chrome path остаются следующими срезами.

`fit_plot_range2d()`, `make_plot_ticks2d()` и `measure_plot_text2d()` образуют
value-only layout boundary. Tick spacing и font size задаются в logical pixels
и масштабируются через `pixel_scale`; возвращаемые ranges, values, labels и
text metrics не содержат borrowed backend pointers. Пересчитывать ticks нужно
при изменении range, plot-area extent или pixel scale; text metrics — при
изменении текста, font, logical size или pixel scale. `PlotEngine2D` использует
эти же utilities как reference path.

C ABI `tc_plot_layout2d.h` экспортирует fit, detached tick values и UTF-8
formatting без переноса layout policy в native composer. C# слой
`RetainedPlot2D` использует этот ABI вместе с typed handle-only wrappers для
`PlotProjection2D`, `PlotGridItem2D`, `PlotLineSeriesItem2D` и
`PlotScatterSeriesItem2D`. Измерение текста выполняется через тот же
`GpuHost`/`FontAtlas`, который будет рисовать подписи; managed-код не получает
font или backend pointer.

`PlotAnnotationLayer2D` принадлежит plot engine и хранит semantic annotations
через generation handles. Одна annotation может проецироваться в несколько
visual items с независимыми фазами `Underlay`, `Overlay`, `Chrome` и clipping
к plot area либо viewport. Поддерживаются data anchors, series-point
references, axes fractions и viewport pixels.

Готовый `PlotDataMarker2D` создаётся через `create_data_marker()`. Он объединяет
data-anchored точку, plot-clipped leader, pixel-sized callout и text, hover,
captured drag с обновлением data position, optional snapping и semantic
действие `close`. Все visual-части маркера клипуются plot area; сама semantic
annotation продолжает существовать и проецироваться за её пределами. Input
annotation маршрутизируется перед plot navigation.

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

The `Plot2D` widget forwards the marker API without exposing its engine.
Run the interactive retained-marker example with:

```bash
sdk/bin/termin_python tcplot/examples/demo_marker.py
```

Its anchor snaps to the plotted curve while dragging, and the callout close
button destroys the annotation through its generation handle.
