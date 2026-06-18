# termin-mesh / tmesh

`termin-mesh` содержит canonical mesh/resource data layer и Python-пакет
`tmesh`. Стандартная asset integration для standalone mesh files живет в
`termin-default-assets`, чтобы mesh domain package не зависел от
`termin-assets`.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-mesh--tmesh)
- [termin-base](../../termin-base/docs/index.md)

## Основные области

- C/C++ headers в `include/tgfx/` для `tc_mesh`, `tc_texture` и resource registry primitives.
- Реализация resource containers в `src/resources/`.
- Python bindings в `python/bindings/`.
- Python пакет `tmesh` в `python/tmesh/`.
- Compatibility re-exports в `python/termin/mesh/` для старых asset import
  paths. Канонические `MeshAsset`, `MeshSpec`, OBJ/STL loaders и mesh
  import/runtime plugins находятся в `termin.default_assets.mesh`.
- Tests в `tests/`.

## Публичный API

Python:

```python
import tmesh
from termin.default_assets.mesh.asset import MeshAsset
from termin.default_assets.mesh.asset_plugin import register_mesh_import_plugin
```

C/C++ API публикуется через installed headers из `include/`.

`tc_mesh` и `tc_texture` считаются canonical engine resources. Renderer/device-specific upload и handle adapters должны оставаться отдельным слоем поверх этих типов.

`termin.mesh.{asset,asset_plugin,mesh_spec,obj_loader,stl_loader}`,
`termin.assets.mesh_asset`, `termin.assets.mesh_plugin` and
`termin.loaders.{mesh_spec,obj_loader,stl_loader}` are compatibility re-exports
during migration.
