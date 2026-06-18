# termin-glb

`termin-glb` owns Termin's GLB/glTF importer, GLB asset integration, and
runtime scene instantiation helpers.

The package contains:

- `termin.glb.loader` for parsing `.glb` and JSON `.gltf` files into GLB scene
  data.
- `termin.glb.asset.GLBAsset` for lazy GLB loading and embedded mesh,
  skeleton, and animation child assets through the generic `termin-assets`
  embedded-asset API.
- `termin.glb.asset_plugin` import/runtime plugins for asset discovery and hot
  reload.
- `termin.glb.instantiator.instantiate_glb` for creating scene entities and
  render/skeleton/animation components from a GLB asset.
- `termin.glb.extractor` for editor/tooling extraction of meshes, textures, and
  animations.

Boundary note: GLB is a multi-domain importer, not a mesh-domain asset. The
package may depend on mesh, render, skeleton, animation, graphics, and default
asset adapters. It must not depend on `termin-app`; editor UI and drag/drop
orchestration stay in `termin-app`. GLB registers embedded mesh, skeleton,
animation, and texture assets through generic `termin-assets` runtime registry
APIs; builtin material lookup is still provided by the composing application
resource manager.
