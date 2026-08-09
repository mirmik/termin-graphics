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

## Planned native importer

The accepted [Native cgltf Importer decision](../../docs/architecture-council/2026-08-09-native-cgltf-importer.md)
defines the migration of the heavy GLB data path to a pinned cgltf backend.
The runtime asset contract remains sequential: native parsing and CPU resource
preparation are followed by deterministic publication into declared embedded
resources. This migration does not add concurrent `Asset.ensure_loaded()`.

The planned backend keeps mapped GLB storage alive only while cgltf accessors
refer to it, builds Termin vertex layouts without Python per-index processing,
and publishes complete meshes through a checked transactional `termin-mesh`
builder. Until the migration card that switches the default backend is
completed, the Python loader remains the production implementation described
above.

The pinned dependency revision and fork policy are documented in
[`native-cgltf.md`](native-cgltf.md).
