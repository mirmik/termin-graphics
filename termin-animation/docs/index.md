# termin-animation

`termin-animation` содержит animation clip/runtime API и Python bindings.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-animation)
- [termin-skeleton](../../termin-skeleton/docs/index.md)
- [canonical naming](../../docs/architecture/2026-03-15-canonical-naming.md)

## Основные области

- Public headers в `include/`.
- Implementation в `src/`.
- Python package в `python/termin/animation`.
- Component wrapper namespace в `python/termin/animation_components`.

## Публичный API

Python packages: `termin.animation` and `termin.animation_components` through
`termin-animation`.

The native component implementation lives in `termin-components-animation`;
the Python wrapper namespace is shipped by `termin-animation` so importer and
runtime packages can use `AnimationPlayer` without depending on `termin-app`.
