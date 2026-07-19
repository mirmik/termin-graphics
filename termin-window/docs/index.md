# termin-window

`termin-window` is the lightweight installed SDK boundary for native windows
that own a Termin graphics runtime and presentation surface. It depends on
`termin-base`, `termin-graphics`, and the selected platform window provider,
but not on scene, engine input, render, or materials packages.

The SDL implementation is available as `termin::SDLBackendWindow` when the SDK
is built with SDL support. Applications link it through:

```cmake
find_package(termin_window CONFIG REQUIRED)
target_link_libraries(app PRIVATE termin_window::termin_window)
```

Engine input routing remains in `termin-display` and is attached through its
SDL input bridge. Native UI applications should eventually consume the
higher-level public gui-native host rather than route SDL events themselves.
