# termin-nodegraph

`termin-nodegraph` содержит toolkit-neutral graph engine и native UI projection.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-nodegraph)
- [README](../README.md)
- [termin-gui-native](../../termin-gui-native/README.md)

## Основные области

- Python package в `python/tcnodegraph`.
- Runnable native example в `tcnodegraph.example`.
- Tests в `tests/`.

## Публичный API

Python package: `tcnodegraph` через distribution `termin-nodegraph`.

После сборки SDK пример запускается командой
`./sdk/bin/termin_python -m tcnodegraph.example`. Параметры `--frames` и
`--seconds` ограничивают время жизни окна для автоматизированного smoke.
