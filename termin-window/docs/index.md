# termin-window

`termin-window` is the lightweight installed SDK boundary for native windows
that own a Termin graphics runtime and presentation surface. It depends on
`termin-base`, `termin-graphics`, and the selected platform window provider,
but not on scene, engine input, render, or materials packages.

Portable applications create a `termin::BackendWindow` through
`termin::create_native_window()` and consume `termin::WindowEvent`. The public
application path covers title, logical/framebuffer size, text input, event
polling, graphics context, and presentation without exposing SDL types.

The concrete `termin::SDLBackendWindow` remains available as an escape hatch
for integrations that need an SDL native handle. Applications link through:

```cmake
find_package(termin_window CONFIG REQUIRED)
target_link_libraries(app PRIVATE termin_window::termin_window)
```

Engine input routing remains in `termin-display` and is attached through its
neutral window input bridge. `termin_gui_native::window_input` translates the
portable pointer, wheel, keyboard, text, and HiDPI coordinates into native GUI
document events. A higher-level application host will own the remaining render
target, font, shader-tool, and frame-loop lifecycle.
