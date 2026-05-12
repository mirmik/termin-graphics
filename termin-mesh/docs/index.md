# termin-mesh / tmesh

`termin-mesh` содержит canonical mesh/resource data layer и Python-пакет `tmesh`.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-mesh--tmesh)
- [termin-base](../../termin-base/docs/index.md)

## Основные области

- C/C++ headers в `include/tgfx/` для `tc_mesh`, `tc_texture` и resource registry primitives.
- Реализация resource containers в `src/resources/`.
- Python bindings в `python/bindings/`.
- Python пакет `tmesh` в `python/tmesh/`.
- Tests в `tests/`.

## Публичный API

Python:

```python
import tmesh
```

C/C++ API публикуется через installed headers из `include/`.

`tc_mesh` и `tc_texture` считаются canonical engine resources. Renderer/device-specific upload и handle adapters должны оставаться отдельным слоем поверх этих типов.

