# termin-skeleton

`termin-skeleton` содержит skeleton-domain API и Python bindings.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-skeleton)
- [canonical naming](../../docs/architecture/2026-03-15-canonical-naming.md)

## Основные области

- Public headers в `include/`.
- Implementation в `src/`.
- Python package в `python/termin/skeleton`.

## Публичный API

The `termin-skeleton` distribution contains only the portable
`termin.skeleton` domain package. Entity synchronization and the
`termin.skeleton_components` wrapper are shipped by the Termin-owned
`termin-components-skeleton` distribution.

`TcSkeleton.set_bones()` is the canonical bulk publication boundary. It
validates the complete hierarchy and finite column-major inverse-bind/TRS payload,
allocates replacement bones and roots, then swaps them atomically. Invalid
parents, cycles, malformed rotations, or allocation failure preserve the old
skeleton and its version. `alloc_bones()` remains a low-level compatibility API
and must not be used for reloadable asset publication.
