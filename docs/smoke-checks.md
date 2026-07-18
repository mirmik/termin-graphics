# Smoke Checks

This page lists repeatable smoke checks for render, shader, graphics backend,
and packaged runtime changes. Prefer the central scripts first, then run the
targeted checks that match the touched subsystem.

## Default Repository Gate

Use the default repository gate during normal development. It runs the working
set: C/C++ tests without window creation, Python tests except those marked
`full`, and no editor-process smoke tests.

```bash
./run-tests.sh
```

Use the full repository gate before merging broad SDK, render, shader, module,
or build-system changes:

```bash
./run-tests.sh --full
```

The full gate enables C++ window tests, Python tests marked `full`, and
editor-process module hot-reload smokes. On headless Linux the smoke scripts use
`xvfb-run` when available. If the editor smoke stage is not relevant or the host
cannot launch the editor, run `./run-tests.sh --full --no-editor-smoke`.

## Render And Shader Matrix

| Area | Command | What It Proves |
| --- | --- | --- |
| C/C++ render and graphics tests | `./run-tests-cpp.sh --full --sdl` | Builds and runs the registered CTest graph, including tgfx2 OpenGL/Vulkan-capable tests where the host supports them. |
| SDF font atlas headless check | `ctest --test-dir build/Release-tests -R '^tgfx2_sdf_test$' --output-on-failure` | FontAtlas bitmap/SDF glyph metrics, UVs, measurements, and CPU atlas signal work without a GL/Vulkan/D3D runtime. |
| Python shader/material/project tests | `./run-tests-python.sh termin-materials/tests/test_shader_parser.py termin-voxels/tests/test_voxel_shader.py termin-shader-runtime/tests/test_shader_tool_resolution.py termin-app/tests/test_shader_tool_resolution.py` | Parser/runtime shader import, built-in shader catalog, and shader tool resolution stay usable from Python. |
| OpenGL tgfx2 smoke | `ctest --test-dir build/Release-tests -R '^(tgfx2_smoke|tgfx2_opengl_bound_resource_set)$' --output-on-failure` | OpenGL device, render target readback, shader artifacts, and bound-resource-set path work. |
| Vulkan tgfx2 smoke | `ctest --test-dir build/Release-tests -R '^(tgfx2_vulkan_smoke|tgfx2_vulkan_window)$' --output-on-failure` | Vulkan offscreen/window paths, readback, resource binding, vertex formats, and optional Slang artifact path work. |
| D3D11 bound path on Windows | `pwsh scripts/validate-tgfx2-d3d11-bound-path.ps1` | Windows D3D11 device smoke and backend-facing bound-resource-set path work. |
| Backend window presentation | `ctest --test-dir build/Release-tests -R '^(backend_window_triangle|backend_window_triangle_immediate|backend_window_triangle_vulkan_vsync|backend_window_triangle_vulkan_immediate|backend_window_d3d11_present)$' --output-on-failure` | SDLBackendWindow VSync and Immediate presentation work on OpenGL and Vulkan, with unavailable window backends skipped by the test. |
| Engine frame limiter | `ctest --test-dir build/Release-tests -R '^termin_engine_frame_cadence_test$' --output-on-failure` and `./run-tests-python.sh termin-engine/tests/test_engine_frame_limit.py` | Positive `target_fps` values provide cadence deadlines, while zero selects Unlimited mode without a software frame deadline. |
| Native UI headless QA | `ctest --test-dir build/Release-tests -R '^termin_gui_native_(showcase_test|renderer_pixel_smoke)$' --output-on-failure` | Stable C++ showcase structure plus real offscreen pixels for texture, text, rounded geometry and nested clipping on compiled Vulkan/D3D11 headless backends. |
| Native UI Python showcase | `sdk/bin/termin_python --termin-overlay build/python-envs/test/overlay.json -m pytest termin-gui-native/python/tests/test_gui_native.py -q` | The Python-built native control tree has stable layout/paint totals, shared models, focus reachability, clipping and long UTF-8 fixtures. |
| Built-in render pass shaders | `ctest --test-dir build/Release-tests -R '^termin_render_passes_builtin_shader_sources_test$' --output-on-failure` | Built-in shader sources/catalog entries and render-pass shader templates remain loadable. |
| DebugTrianglePass pixel smoke | `ctest --test-dir build/Release-tests -R '^termin_render_passes_debug_triangle_pixel_smoke$' --output-on-failure` | Vulkan offscreen execution of a real `termin-render-passes` FramePass, built-in shader catalog lookup, RenderContext2 pass recording, texture output, and pixel readback work. |
| ColorPass mesh/material pixel smoke | `ctest --test-dir build/Release-tests -R '^termin_render_passes_color_pass_pixel_smoke$' --output-on-failure` | Vulkan offscreen `ColorPass` rendering of a scene `MeshRenderer`, opaque material phase routing, material UBO color binding, mesh bridge draw, and pixel readback work. |

For a deterministic native UI desktop capture, build the examples and request
one frame. The output is a binary 800×600 PPM by default and the process exits
after successful readback:

```bash
cmake -S termin-gui-native -B build/termin-gui-native-examples \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$PWD/sdk" \
  -DTERMIN_GUI_NATIVE_BUILD_TESTS=OFF -DTERMIN_GUI_NATIVE_BUILD_EXAMPLES=ON
cmake --build build/termin-gui-native-examples -j
TERMIN_GUI_NATIVE_SCREENSHOT=/tmp/termin-gui-native-showcase.ppm \
  build/termin-gui-native-examples/termin_gui_native_showcase
```

On a headless Linux host, set `SDL_VIDEODRIVER=offscreen` and
`TERMIN_BACKEND=opengl`. A Vulkan-capable window driver or display should use
the normal default backend; unsupported SDL/backend combinations return the
example skip code `77` with an error message.

## Packaged Runtime Matrix

| Area | Command | What It Proves |
| --- | --- | --- |
| Runtime package exporter | `./run-tests-python.sh --full termin-project-build/tests/test_runtime_package_exporter.py termin-player/tests/test_runtime_package_loader.py` | Runtime manifests, shader artifacts, assets, and loader behavior remain coherent. |
| Desktop build profile path | `./run-tests-python.sh termin-project-build/tests/test_project_build_profile_backend.py termin-project-build/tests/test_project_build_target_common.py termin-project-build/tests/test_project_build_target_preflight.py` | Build profile dispatch, target preflight, and host/tool checks remain deterministic. |
| Relocatable desktop bundle | Run the project-specific relocated bundle smoke, for example the Chess bundle smoke tracked separately. | The packaged host can run outside the source tree with bundled assets, Python packages, and shader artifacts. |

## CI Jobs To Watch

The GitHub Actions workflow keeps the same checks split by cost and platform:

- `lint-cpp` keeps the native static-analysis gate from regressing;
  `plan-pr-linux` runs the repository check profile, including source-size
  policy, manifest validation, and orphan detection.
- `build-cpp-libs`, `test-cpp-libs`, `build-bindings`, `test-termin`, and
  `test-pip-packages` cover the Linux SDK build and test path.
- `tgfx2-d3d11-bound-windows` covers the Windows D3D11 backend smoke.
- `smoke-build-system-windows` covers the Windows build-tool entry points.
