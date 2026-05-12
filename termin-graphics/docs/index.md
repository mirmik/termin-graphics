# termin-graphics

`termin-graphics` содержит tgfx/tgfx2: backend-neutral GPU API, render-device abstraction, context/runtime helpers и reusable rendering utilities.

Связанные документы:

- [Module Map](../../docs/modules.md#termin-graphics--tgfx)
- [tgfx2 migration](migration-tgfx2.md)
- [tgfx2 Python migration](migration-tgfx2-python.md)
- [tcplot C++ migration](migration-tcplot-cpp.md)
- [Renderer facades plan](../../docs/plans/2026-05-12-tgfx-renderer-facades.md)

## Границы

В этот модуль должны попадать GPU abstractions и utilities, которые не знают о frame graph, editor UI или конкретной application domain.

Если код знает про frame graph/debugger/render pipeline, он обычно относится к [termin-render](../../docs/modules.md#termin-render). Если код знает про widget tree, layout или events, он относится к [termin-gui](../../termin-gui/docs/index.md).
