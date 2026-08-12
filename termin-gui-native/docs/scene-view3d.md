# SceneView3D

`SceneView3D` embeds a small `VisualScene3D` in the native retained widget
tree. It is intended for compact, item-oriented views such as object tools,
previews and the orientation cube. A full engine `tc_scene` remains the right
abstraction for entity/component simulation and framegraph-driven worlds.

The widget borrows its `TcVisualScene3D`. The owner must keep the scene alive
until it calls `set_scene({})` or destroys the widget. The widget owns its
offscreen color/depth textures, built-in packet renderers and
`SceneInteraction3D`; it releases those GPU resources through the canonical
`RenderPreparedWidget` lifecycle.

```cpp
auto scene_handle = tc_visual_scene3d_create();
termin::visual::TcVisualScene3D scene{scene_handle};

auto* view = new termin::gui_native::SceneView3D(scene);
document.adopt(view);
document.add_root(*view);

view->set_camera_provider([](termin::gui_native::ViewportSurfaceSize size)
    -> std::optional<termin::gui_native::SceneView3DCamera> {
    return make_camera(size.width, size.height);
});
```

`set_camera()` installs fixed view/projection matrices. A camera provider is
sampled during render preparation after layout, so it sees the actual integer
framebuffer size. `invalidate_view()` requests a new offscreen frame after
external camera state changes; `invalidate_scene()` does the same after items
are mutated.

Widget coordinates are unprojected with the current matrices using Termin's
top-left, Y-down and Z-in-`[0, 1]` clip convention. Pointer events are then
routed to `SceneInteraction3D`. An item hit owns that pointer sequence and the
widget-level fallback is not called. When no item accepts the down event, an
optional fallback may accept the sequence and implement an orbit camera:

```cpp
view->set_fallback_pointer_handler(
    [](auto& view, const tc_ui_pointer_event& event, const auto& world_ray) {
        return camera_controller.handle(event, world_ray, view.framebuffer_size());
    });
```

The fallback is a widget policy only; neither `VisualScene3D` nor its items
know about cameras or gizmos. A gizmo is an ordinary scene item implementing
paint and hit-test contracts.

For a corner orientation control, place a small `SceneView3D` above a larger
`Viewport3D` with `OverlayLayout`. Normal retained-tree ordering and hit testing
keep pointer capture local to the small view; no special 3D overlay primitive
is required.
