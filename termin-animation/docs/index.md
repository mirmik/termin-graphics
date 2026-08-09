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

## Bulk track contract

`TcAnimationClip.set_tracks()` atomically replaces a clip with owned flat
tracks identified by source node index, path, interpolation, component count,
times, and values. The format keeps vec3 scale and the glTF CUBICSPLINE
`in/value/out` tensor shape. LINEAR and STEP translation/rotation/scale tracks
are sampled by the runtime, including shortest-path quaternion interpolation.
CUBICSPLINE and morph-weight tracks remain round-trippable but sampling them is
an explicit error until those player paths are implemented. The legacy
name-grouped channel API remains available only for existing assets.
