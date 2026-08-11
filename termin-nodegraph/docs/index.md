# termin-nodegraph

`termin-nodegraph` содержит toolkit-neutral graph engine и native UI projection.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-nodegraph)
- [README](../README.md)
- [termin-gui-native](../../termin-gui-native/README.md)

## Основные области

- Headless C++ core в `include/termin/nodegraph` и `src`.
- Стабильный C ABI в `include/termin/nodegraph/c_api.h`.
- Native Python binding и snapshot-обёртки в `python/tcnodegraph`.
- Runnable native example в `examples/native_nodegraph_demo.py`.
- Tests в `tests/`.

## Публичный API

CMake target: `termin_nodegraph::core`. C++ API объявлен в
`termin/nodegraph/graph.hpp`, C ABI — в `termin/nodegraph/c_api.h`.
Сериализация использует глубоко владеемые `tc_value`-деревья либо JSON.
Десериализация транзакционна; невалидные ID, endpoints, socket types и
cardinality отклоняются без частичной замены графа.

Python package: `tcnodegraph` через distribution `termin-nodegraph`.
`Graph` владеет C++ core, а Python-объекты узлов, рёбер и групп являются
отсоединёнными снимками. Все изменения проходят через `GraphController` и
повторно проверяются native core; прямое изменение снимка граф не меняет.

После сборки SDK пример запускается командой
`./sdk/bin/termin_python termin-nodegraph/examples/native_nodegraph_demo.py`.
Параметры `--frames` и `--seconds` ограничивают время жизни окна для
автоматизированного smoke. Оконный host принадлежит тонкому пакету
`termin-window` и доступен в SDL-enabled профилях `graphics` и `full`.
Параметр `--offscreen` создаёт PNG без оконного backend; путь задаётся через
`--output`.
