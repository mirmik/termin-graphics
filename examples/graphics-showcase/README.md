# Graphics profile showcase

This is the cross-package acceptance example for the Termin `graphics` SDK
profile. It deliberately uses the installed SDK as a product: run it with the
bundled isolated Python and do not add the repository to `PYTHONPATH`.

```bash
./build-sdk.sh --profile graphics --no-sdl
sdk/bin/termin_python -I examples/graphics-showcase/main.py \
  --headless \
  --output /tmp/termin-graphics-showcase.png \
  --report /tmp/termin-graphics-showcase.json
```

After building the SDK, the repository gate performs the same run with poisoned
ambient Python paths and validates the profile boundary, report and artifact:

```bash
scripts/smoke-graphics-showcase
```

The headless path is the required contract and works without `termin-window`,
SDL, `termin-display`, engine runtime, editor, or PySDL2. It uses
`termin.gui_native.OffscreenGuiComposition`; failure of any section is logged
with its name and makes the process fail.

## Feature matrix

| Section | Product surface |
|---|---|
| `native_ui` | Retained controls, collections, text, models and layout |
| `plot_2d` | Retained Plot2D lines, append/update path, axes and labels |
| `plot_3d` | Plot3D line, scatter, shaded surface and colormap |
| `visual_scene_nodegraph` | Visual-scene primitives, nodegraph model and projection |
| `plot_nodegraph_composition` | Plot2D and Plot3D embedded as node-body widgets |

The JSON report records the exact imported graphics-profile packages, every
declared section, framebuffer coverage metrics and the final artifact path.
The requested PNG is produced by the composition section rather than copied
from a golden image.

`termin-window` is conditional on an SDL-enabled build and is therefore not
imported by the mandatory headless gate. An SDL-enabled graphics SDK also has
an interactive frontend for the integration section:

```bash
./build-sdk.sh --profile graphics --sdl
sdk/bin/termin_python -I examples/graphics-showcase/main.py --windowed
```

The frontend opens on an overview and exposes every registry section as a tab;
it uses `termin.window`, while engine-level `termin.display` remains outside
this profile. For automated checks, `--frames N` and `--seconds N` bound the
window lifetime.

On Windows, build the D3D11-only product and run the same Python entry point:

```powershell
.\build-sdk.ps1 --profile graphics --no-sdl --no-vulkan --no-opengl
.\sdk\bin\termin_python.exe -I .\examples\graphics-showcase\main.py `
  --headless --output $env:TEMP\termin-graphics-showcase.png `
  --report $env:TEMP\termin-graphics-showcase.json
```
