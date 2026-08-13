# Graphics SDK showcase

This is the cross-package acceptance example for the composed Termin Core +
Graphics SDK. The repository gate creates that composition in a temporary
directory and runs it with bundled isolated Python, without adding the source
checkout to `PYTHONPATH`:

```bash
TERMIN_SLANGC=/absolute/path/to/slangc task test -- \
  --core-sdk /absolute/path/to/termin-core/sdk --no-sdl
```

After building the Graphics layer, the same installed-consumer gate can be
rerun with poisoned ambient Python paths:

```bash
TERMIN_SLANGC=/absolute/path/to/slangc task smoke -- \
  --core-sdk /absolute/path/to/termin-core/sdk
```

The headless path is the required contract and works without `termin-window`,
SDL, `termin-display`, engine runtime, editor, or PySDL2. It uses
`termin.gui_native.OffscreenGuiComposition`; failure of any section is logged
with its name and makes the process fail.

## Feature matrix

| Section | Product surface |
|---|---|
| `native_ui` | Retained controls, collections, text, models and layout |
| `graphics_lines` | Mesh-expanded line caps, joins, widths and 3D polylines |
| `tcplot_sine` | Sine, cosine and damped-sine line families |
| `tcplot_scatter` | Three clustered scatter series and a trend line |
| `tcplot_multi` | Polynomial and damped-oscillation plots side by side |
| `tcplot_marker` | Draggable retained marker with nearest-sample snapping |
| `tcplot_helix` | Double helix and deterministic 3D scatter |
| `tcplot_surface` | Sinc surface, Viridis colorbar, wireframe and z scaling |
| `visual_scene_gallery` | Retained shapes, hierarchy, transforms, opacity, z-order and hit regions |
| `animated_skinned_glb` | Loaded GLB mesh, two-joint skeleton and sampled animation pose |
| `visual_scene_nodegraph` | Visual-scene primitives, nodegraph model and projection |
| `visual_scene3d_widget` | Retained 3D items, camera provider, orbit fallback and item actions in `SceneView3D` |
| `plot_nodegraph_composition` | Plot2D and Plot3D embedded as node-body widgets |

The remaining profile packages are exercised as supporting parts of those
pages rather than represented by artificial empty tabs: `termin-mesh` builds
the expanded line geometry, `termin-shader-runtime` compiles its shaders,
`termin-image` writes the acceptance PNG, and `termin-base`, `termin-dispatch`,
`termin-inspect`, `termin-tween`, the build tools and the
nanobind SDK are verified by the isolated import boundary. `termin-window` is
the optional interactive host described below.

The JSON report records the exact imported graphics-profile packages, every
declared section, framebuffer coverage metrics and the final artifact path.
The requested PNG is produced by the composition section rather than copied
from a golden image.

`termin-window` is conditional on an SDL-enabled build and is therefore not
imported by the mandatory headless gate. An SDL-enabled graphics SDK also has
an interactive frontend for the integration section:

```bash
task build -- --core-sdk /absolute/path/to/termin-core/sdk --sdl
```

The resulting `sdk/` directory is a Graphics layer, not a directly runnable
prefix. Compose it over the recorded Core SDK before launching the windowed
showcase:

```bash
/absolute/path/to/composed-sdk/bin/termin_python -I \
  examples/graphics-showcase/main.py --windowed
```

The frontend opens on an overview and exposes every registry section as a tab;
it uses `termin.window`, while engine-level `termin.display` remains outside
this profile. For automated checks, `--frames N` and `--seconds N` bound the
window lifetime.

The repository currently exposes and verifies this product workflow on Linux.
Windows will get a public Task entry only together with an independently
verified installed-Core build path.
