# termin-render

Модуль публикует два слоя rendering framework: scene-neutral
`termin_render_core` и зависящий от него scene adapter `termin_render`.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-render)
- [termin-graphics](../../termin-graphics/docs/index.md)

## Основные области

- C++ public headers в `include/`.
- Render framework implementation в `src/`.
- Scene render mount config C/C++ wrappers: `tc_viewport_config`,
  `tc_render_target_config`, `ViewportConfig`, `RenderTargetConfig`.
- C++ scene render extension helpers: render-state accessors, render
  mount config/template accessors, and legacy render mount/state migration
  adapter.
- Python package/bindings в `python/termin/render*`.
- Tests в `tests/`.

## Execution boundary

`ExecuteContext` — scene-neutral контракт исполнения pass: GPU resources,
attachments, viewport, `RenderViewState`, immutable render-item snapshot и
diagnostics. Game-scene данные не являются его обязательной частью.

Pass, которому нужны `TcSceneRef`, lights, internal entities или scene masks,
явно запрашивает `SceneRenderServices`. Scene-specific shader discovery так же
объявляется отдельным `SceneShaderUsageProvider`, а не методом generic
`CxxFramePass`.

`RenderEngine::execute_pipeline(RenderExecution)` принимает только нейтральные
targets, заранее опубликованные immutable snapshots и type-safe
`RenderExecutionCapabilities`. `render_scene_pipeline_offscreen()` находится в
отдельном scene adapter header, собирает snapshots/services до execution и не
дублирует scheduler/resource implementation.

`RenderItemSource::publish()` задаёт общий lifecycle публикации snapshot:
source получает нейтральные view/mask inputs и наполняет только mutable
`RenderItemCollection`, после чего базовый contract атомарно публикует snapshot
либо инвалидирует частичный результат с диагностикой. `TcSceneRenderItemSource`
является scene adapter implementation; executor и generic source header не
знают о `tc_scene`, entities, components или lights. Они собираются в
отдельный `termin_render_core`, тогда как scene traversal/services остаются в
`termin_render`.

Для adapter-specific CPU data `RenderItemCollection` предоставляет
type-erased ownership: source сохраняет `shared_ptr<const Payload>`, а
`tc_render_item::source.adapter_data` указывает на удерживаемое snapshot
значение. Core не знает concrete payload type; invalidation уничтожает payload
ownership, но сохраняет capacity контейнера для следующей публикации. Borrowed
pointers на mutable source slots в immutable snapshot недопустимы.

Non-texture framegraph resources расширяют executor через
`FrameGraphResourceTypeDescriptor`. Registry хранит cold-path factory и
необязательный sampled-texture callback с явным `Color`/`Depth` attachment
kind; `PipelineRenderCache` владеет
созданными `FrameGraphResource`, а каждый pass получает только объявленные
reads/writes через `ExecuteContext::frame_graph_resources`. Неизвестный kind
является logged validation error и не превращается неявно в FBO.

## Публичный API

Python package: `termin.render` / `termin.render_framework` через пакет `termin-render`.

C++ API публикуется через headers из `include/`. Scene-neutral consumers
используют CMake package/target
`termin_render_core::termin_render_core`; engine scene consumers —
`termin_render::termin_render`.

`termin.render.ViewportConfig` and `termin.render.RenderTargetConfig` are the
canonical Python bindings for scene render mount configuration, with matching
dict serialization helpers exported from `termin.render`.

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
- `shadow_map_array` — зарегистрированный `termin-render-passes` ресурс;
  generic executor знает только его `FrameGraphResource` contract и sampled
  preview.

`FboSplit` and `FboJoin` are compile-time graph utility nodes. They do not
create runtime passes or `tc_pass` instances. Direct conversion between `fbo`,
`color_texture`, and `depth_texture` must go through these nodes so the compiler
can record `ResourceView` and `FboComposition` metadata in `PipelineRenderCache`.

## Связь с termin-graphics

`termin-render` использует backend-neutral primitives из `termin-graphics`. Generic GPU utilities без знания frame graph обычно должны жить в `termin-graphics`; frame graph, render pipeline и debugger logic остаются здесь.
