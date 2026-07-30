# termin-window

`termin-window` is the lightweight installed SDK boundary for native windows
and platform presentation. `tgfx::GraphicsHost` in `termin-graphics` is the
only owner of the application `IRenderDevice`, `PipelineCache` and
`RenderContext2`; each `BackendWindow` owns only its native window and
per-window presentation resources. It depends on
`termin-base`, `termin-graphics`, and the selected platform window provider,
but not on scene, engine input, render, or materials packages.

Portable applications use `termin::create_native_windowed_graphics()` to get a
`WindowedGraphicsSession`. The session is a lifetime aggregate of the platform
`BackendWindowSystem` and the canonical `tgfx::GraphicsHost`, not another
graphics abstraction. Applications obtain renderer state from
`session->graphics()` and create any number of equal `BackendWindow`
presentation targets with `session->create_window()`. All GPU consumers and
windows must be destroyed before `session->close()`.
Each window reports the `GraphicsHost` identity that created it; this is an
identity/validation hook for injected composition and does not expose device
or context ownership through the window abstraction.
Windows consume `termin::WindowEvent`; the public application path covers
title, logical/framebuffer size, text input, event polling, semantic system
cursors, clipboard text, and presentation without exposing
SDL types.
Each window also reports its current per-window `content_scale` (physical
framebuffer pixels per logical coordinate). Resize events include the sampled
scale, and moving an SDL window to a display with another scale emits
`DisplayScaleChanged`; no process-global UI scale is stored.
SDL's process-global queue is routed through per-window pending queues, so
polling one `BackendWindow` does not consume events addressed to another and a
global quit request reaches every registered window.

The target framework-neutral `WindowManager` owns a collection of
`BackendWindow` objects created on one borrowed `WindowedGraphicsSession`. It
publishes stable generational handles, ordered per-window event batches and
deterministic close order. Application code owns the mapping from those
handles to UI-framework or raw-rendering content, render scheduling,
main/secondary roles and exit policy. See
[Framework-Neutral Window Management](../../docs/architecture/2026-07-23-framework-neutral-window-management.md).

The concrete `termin::SDLBackendWindow` remains available as an escape hatch
for integrations that need an SDL native handle. Applications link through:

```cmake
find_package(termin_window CONFIG REQUIRED)
target_link_libraries(app PRIVATE termin_window::termin_window)
```

Engine input routing remains in `termin-display` and is attached through its
neutral window input bridge. `termin_gui_native::window_adapter` translates
portable pointer, wheel, keyboard, text, and HiDPI coordinates into native GUI
document events and borrows one manager-owned window for platform services and
presentation. It never owns or closes that window. Applications retain window,
graphics-session, loop, and exit-policy ownership. `termin-window` does not
depend on either UI target.
