# termin-graphics

`termin-graphics` содержит tgfx/tgfx2: backend-neutral GPU API, render-device abstraction, context/runtime helpers и reusable rendering utilities.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-graphics--tgfx)
- [Architecture notes](architecture/index.md)
- [Renderer facades plan](../../docs/plans/2026-05-12-tgfx-renderer-facades.md)

## Границы

В этот модуль должны попадать GPU abstractions и utilities, которые не знают о frame graph, editor UI или конкретной application domain.

Если код знает про frame graph/debugger/render pipeline, он обычно относится к [termin-render](../../docs/modules.md#termin-render). Если код знает про widget tree, layout или events, он относится к [termin-gui](../../termin-gui/docs/index.md).

## Texture CPU Sync

`tc_texture_storage_kind` describes the source of truth:

- `TC_TEXTURE_STORAGE_CPU_FIRST` — pixels in `tc_texture::data` are authoritative.
- `TC_TEXTURE_STORAGE_GPU_FIRST` — GPU image is authoritative; CPU data can be
  populated on demand.

`tc_texture_sync_to_cpu(tc_texture*)` is a no-op for CPU-first textures. For
GPU-first textures it asks the active `tgfx_gpu_ops` backend to read back the
image into `tc_texture::data`. Python `Texture.sync_to_cpu()` exposes the same
operation, and preview helpers use it transparently when CPU pixels are absent.

## tgfx2 Backend Contract

Внешний tgfx2 API не должен требовать от вызывающего кода знания OpenGL/Vulkan
деталей. Например, CPU row 0 и shader `v=0` считаются верхом изображения для
всех backend-ов; если нативный backend живет в другой системе координат, он
переворачивает upload/sampling/readback внутри себя.

Для кода, которому всё-таки нужно узнать контракт устройства, используйте
`IRenderDevice::capabilities()` или Python `Tgfx2Context.texture_origin_top_left`,
а не ветвление по строке `Tgfx2Context.backend`. Строковый backend оставлен как
диагностика, не как точка принятия rendering-решений.
