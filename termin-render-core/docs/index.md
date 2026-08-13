# termin-render-core

`termin-render-core` is the scene-neutral render orchestration layer of the
Graphics SDK. It owns framegraph execution, render pipelines, immutable render
item snapshots, render-task planning, material-pipeline contracts and shader
ABI validation.

The module accepts render targets, render items and explicitly provided
capabilities. It does not traverse an engine scene, own entities or components,
load projects, or define lighting and application policy. Those concerns must
be supplied by adapters in upper repositories.

## Public surface

- CMake package and target: `termin_render_core::termin_render_core`;
- C and C++ headers under `include/`;
- process-level execution tuning through `tc_render_core_settings.h`;
- no Python package and no scene adapter.

## Execution boundary

`RenderItemSource::publish()` creates an immutable snapshot from neutral
view/mask inputs. `RenderEngine::execute_pipeline(RenderExecution)` consumes
those snapshots together with caller-owned targets and type-safe execution
capabilities. Concrete scene traversal and capability lifetime remain the
adapter's responsibility.

Framegraph resources are described by `FrameGraphResourceTypeDescriptor`.
Unknown resource kinds and invalid contracts are logged validation errors;
they do not silently become framebuffer resources.

## Related contracts

- [Pipeline template identity](architecture/pipeline-template.md)
- [Render-item task planning](render-item-task-planning.md)
- [Unknown pass lifecycle](unknown-pass.md)
