# Smoke Checks

This page lists repeatable smoke checks for render, shader, graphics backend,
and packaged runtime changes. Prefer the central scripts first, then run the
targeted checks that match the touched subsystem.

## Default Repository Gate

Use the full repository gate before merging broad SDK, render, shader, module,
or build-system changes:

```bash
./run-tests.sh
```

On headless machines this includes editor-process module hot-reload smokes
through `xvfb-run` when available. If the editor smoke stage is not relevant or
the host cannot launch the editor, run:

```bash
./run-tests.sh --no-editor-smoke
```

## Render And Shader Matrix

| Area | Command | What It Proves |
| --- | --- | --- |
| C/C++ render and graphics tests | `./run-tests-cpp.sh --sdl` | Builds and runs the registered CTest graph, including tgfx2 OpenGL/Vulkan-capable tests where the host supports them. |
| Python shader/material/project tests | `./run-tests-python.sh termin-app/tests/shader_parser_test.py termin-voxels/tests/test_voxel_shader.py termin-app/tests/test_shader_tool_resolution.py` | Parser/runtime shader import, built-in shader catalog, and shader tool resolution stay usable from Python. |
| OpenGL tgfx2 smoke | `ctest --test-dir build/Release-tests -R '^(tgfx2_smoke|tgfx2_opengl_bound_resource_set)$' --output-on-failure` | OpenGL device, render target readback, shader artifacts, and bound-resource-set path work. |
| Vulkan tgfx2 smoke | `ctest --test-dir build/Release-tests -R '^(tgfx2_vulkan_smoke|tgfx2_vulkan_window)$' --output-on-failure` | Vulkan offscreen/window paths, readback, resource binding, vertex formats, and optional Slang artifact path work. |
| D3D11 bound path on Windows | `pwsh scripts/validate-tgfx2-d3d11-bound-path.ps1` | Windows D3D11 device smoke and backend-facing bound-resource-set path work. |
| Backend window presentation | `ctest --test-dir build/Release-tests -R '^(backend_window_triangle|backend_window_d3d11_present)$' --output-on-failure` | SDLBackendWindow presentation works for the compiled backends, with unsupported window backends skipped by the test. |
| Built-in render pass shaders | `ctest --test-dir build/Release-tests -R '^termin_render_passes_builtin_shader_sources_test$' --output-on-failure` | Built-in shader sources/catalog entries and render-pass shader templates remain loadable. |

## Packaged Runtime Matrix

| Area | Command | What It Proves |
| --- | --- | --- |
| Runtime package exporter | `./run-tests-python.sh termin-app/tests/test_runtime_package_exporter.py termin-app/tests/test_runtime_package_loader.py` | Runtime manifests, shader artifacts, assets, and loader behavior remain coherent. |
| Desktop build profile path | `./run-tests-python.sh termin-app/tests/test_project_build_profile_backend.py termin-app/tests/test_project_build_target_common.py termin-app/tests/test_project_build_target_preflight.py` | Build profile dispatch, target preflight, and host/tool checks remain deterministic. |
| Relocatable desktop bundle | Run the project-specific relocated bundle smoke, for example the Chess bundle smoke tracked separately. | The packaged host can run outside the source tree with bundled assets, Python packages, and shader artifacts. |

## CI Jobs To Watch

The GitHub Actions workflow keeps the same checks split by cost and platform:

- `lint-cpp` and `lint-source-size` keep the static-analysis/source-size gates
  from regressing.
- `build-cpp-libs`, `test-cpp-libs`, `build-bindings`, `test-termin`, and
  `test-pip-packages` cover the Linux SDK build and test path.
- `tgfx2-d3d11-bound-windows` covers the Windows D3D11 backend smoke.
- `smoke-build-system-windows` covers the Windows build-tool entry points.
