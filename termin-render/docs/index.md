# termin-render

`termin-render` содержит rendering framework поверх canonical engine resources: render pipelines, frame graph, render engine bindings и integration helpers.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-render)
- [termin-graphics](../../termin-graphics/docs/index.md)

## Основные области

- C++ public headers в `include/`.
- Render framework implementation в `src/`.
- Python package/bindings в `python/termin/render*`.
- Tests в `tests/`.

## Публичный API

Python package: `termin.render` / `termin.render_framework` через пакет `termin-render`.

C++ API публикуется через headers из `include/` и CMake package `termin_render`.

`termin.render.ImmediateRenderer` is the Python singleton wrapper around
`tgfx.ImmediateRenderer`. The native immediate renderer remains in
`termin-graphics`; `termin-render` owns the shared debug/gizmo instance used by
frame graph passes and tools.

## Render Graph Resources

Render graph sockets/resources distinguish complete framebuffers from sampled
attachments:

- `fbo` — tuple resource with color and optional depth attachments.
- `color_texture` — sampled/view reference to an FBO color attachment.
- `depth_texture` — sampled/view reference to an FBO depth attachment.
- `shadow` — shadow-map resource.

`FboSplit` and `FboJoin` are compile-time graph utility nodes. They do not
create runtime passes or `tc_pass` instances. Direct conversion between `fbo`,
`color_texture`, and `depth_texture` must go through these nodes so the compiler
can record `ResourceView` and `FboComposition` metadata in `PipelineRenderCache`.

## Связь с termin-graphics

`termin-render` использует backend-neutral primitives из `termin-graphics`. Generic GPU utilities без знания frame graph обычно должны жить в `termin-graphics`; frame graph, render pipeline и debugger logic остаются здесь.
