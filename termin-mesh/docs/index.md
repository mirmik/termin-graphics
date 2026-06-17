# termin-mesh / tmesh

`termin-mesh` содержит canonical mesh/resource data layer, Python-пакет
`tmesh` и asset integration для standalone mesh files.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-mesh--tmesh)
- [termin-base](../../termin-base/docs/index.md)

## Основные области

- C/C++ headers в `include/tgfx/` для `tc_mesh`, `tc_texture` и resource registry primitives.
- Реализация resource containers в `src/resources/`.
- Python bindings в `python/bindings/`.
- Python пакет `tmesh` в `python/tmesh/`.
- Asset слой в `python/termin/mesh/`: `MeshAsset`, mesh import/runtime
  plugins, `MeshSpec`, OBJ/STL loaders.
- Tests в `tests/`.

## Публичный API

Python:

```python
import tmesh
from termin.mesh.asset import MeshAsset
from termin.mesh.asset_plugin import register_mesh_import_plugin
```

C/C++ API публикуется через installed headers из `include/`.

`tc_mesh` и `tc_texture` считаются canonical engine resources. Renderer/device-specific upload и handle adapters должны оставаться отдельным слоем поверх этих типов.

`termin.assets.mesh_asset`, `termin.assets.mesh_plugin` and
`termin.loaders.{mesh_spec,obj_loader,stl_loader}` are compatibility re-exports
owned by `termin-app` during migration.
