# termin-render

`termin-render` содержит rendering framework поверх canonical engine resources: render pipelines, frame graph, render engine bindings и integration helpers.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-render)
- [termin-graphics](../../termin-graphics/docs/index.md)
- [render graph resource types plan](../../docs/plans/2026-05-10-render-graph-resource-types.md)
- [render target tc_texture migration](../../docs/plans/2026-04-22-render-target-tc-texture-migration.md)

## Основные области

- C++ public headers в `include/`.
- Render framework implementation в `src/`.
- Python package/bindings в `python/termin/render*`.
- Tests в `tests/`.

## Публичный API

Python package: `termin.render` / `termin.render_framework` через пакет `termin-render`.

C++ API публикуется через headers из `include/` и CMake package `termin_render`.

## Связь с termin-graphics

`termin-render` использует backend-neutral primitives из `termin-graphics`. Generic GPU utilities без знания frame graph обычно должны жить в `termin-graphics`; frame graph, render pipeline и debugger logic остаются здесь.

