# termin-skeleton

`termin-skeleton` содержит skeleton-domain API и Python bindings.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-skeleton)
- [canonical naming](../../docs/architecture/2026-03-15-canonical-naming.md)

## Основные области

- Public headers в `include/`.
- Implementation в `src/`.
- Python package в `python/termin/skeleton`.
- Component wrapper namespace в `python/termin/skeleton_components`.

## Публичный API

Python packages: `termin.skeleton` and `termin.skeleton_components` through
`termin-skeleton`.

The native component implementation lives in `termin-components-skeleton`;
the Python wrapper namespace is shipped by `termin-skeleton` so importer and
runtime packages can use `SkeletonController` without depending on `termin-app`.
