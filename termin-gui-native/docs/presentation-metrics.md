# Presentation metrics

`termin-gui-native` represents display density, accessibility font scale and
safe insets with the platform-neutral `tc_ui_presentation_metrics` value. The
value belongs to a document presentation; it is never process-global and does
not query Android, a desktop window backend or environment variables.

## Contract

Metrics contain:

- a finite positive geometry `density_scale`;
- a finite positive `font_scale`;
- a finite positive physical framebuffer extent;
- finite non-negative physical safe insets bounded by that extent.

Invalid values are rejected by `tc_ui_document_set_presentation_metrics` with
an error log. A new document deliberately has no implicit metrics. A desktop
host without a platform scale source must publish an explicit identity value:

```cpp
const auto metrics =
    tc_ui_presentation_metrics_identity(tc_ui_size{width, height});
if (!document.set_presentation_metrics(metrics)) {
    // The setter has logged the rejected value.
}
```

`density_scale == 1` is an identity geometry transform. Logical geometry uses:

```text
logical_extent = physical_extent / density_scale
logical_safe_inset = physical_safe_inset / density_scale
effective_physical_font_scale = density_scale * font_scale
```

The full logical viewport starts at `(0, 0)`. The logical safe rect starts at
the scaled left/top insets and excludes all four scaled insets. Logical bounds
remain fractional; render-boundary pixel snapping is defined by the painter
integration rather than by this value contract.

`NativeDocumentPainter` requires explicit metrics on every document
submission. It rejects a submission whose physical extent differs from the
active render target. At the renderer boundary, non-identity geometry is
multiplied by `density_scale`; points and rectangle edges are rounded to the
nearest physical pixel (half values away from zero), and a positive logical
stroke or radius remains at least one physical pixel. Rectangle width and
height are derived from their independently snapped edges so clips, fills and
textures share exactly the same boundary. Scale `1.0` preserves coordinates
unchanged. Hit testing uses unsnapped logical bounds, and input uses the exact
inverse division by `density_scale`.

Logical text size is multiplied by `density_scale * font_scale` for
measurement and rasterization. Physical measurements are divided only by
`density_scale` before returning to layout, so accessibility font scale changes
logical text extent and therefore reflow. Glyph storage remains bounded by the
font atlas dimensions, and the per-integer-size metrics cache is capped at 256
entries so repeated scale changes cannot grow it for the lifetime of the
process.

## Widget subtree transforms

Presentation density and widget transforms solve different problems. Density
belongs to the whole document and can cause layout/reflow. A widget
`subtree_transform` is a local translation plus a finite positive uniform scale
applied after layout; it changes presentation and input coordinates without
changing intrinsic sizes or the widget's logical `bounds`.

Transforms compose through widget ancestry. Canonical paint traversal emits
balanced transform commands, child hit testing applies the local inverse, and
pointer bubbling remaps the document point independently for every receiver.
Capture therefore remains handle-based and continues to work if an ancestor's
transform changes during a drag. Overlay geometry stays in document space;
anchor rectangles are mapped to document coordinates before popup placement.

At the renderer boundary geometry uses `density_scale * subtree_scale`.
Direct UI text requests glyphs at
`logical_size * density_scale * font_scale * subtree_scale`. Nested Canvas2D
text derives a raster scale from its accumulated affine transform and requests
the final display size from `FontAtlas`; the atlas itself remains
transform-neutral. This avoids scaling a previously rasterized small bitmap.

Scene views use this contract for widget portals: portal bounds remain in world
logical coordinates and the camera is installed as their subtree transform.
Thus labels, controls, clips, stroke widths and pointer coordinates zoom as one
coherent subtree.

Portal placement is deliberately axis-aligned: it uses the retained item's
world-space AABB. Rotation, shear and non-uniform widget transforms are not
inferred from item ancestry; an application that needs rotated interactive
content must keep that content in the retained scene rather than attach a
native widget portal.

## Desktop window source

`termin-window` exposes `BackendWindow::content_scale()` as physical
framebuffer pixels per logical window coordinate. `GuiWindowAdapter` reads that
value when it is created, before input batches, and before every rendered
frame. A resize or display-scale event invalidates the document and requests a
repaint, so root, overlay, clip, pointer and effective font bounds change as
one document-local presentation revision. Offscreen sinks and other hosts
without a platform scale source explicitly retain `1.0`.

The SDL backend creates visible windows with high-DPI drawables. On Windows it
requests per-monitor-v2 DPI-scaled coordinates before SDL video
initialization, and `SDL_WINDOWEVENT_DISPLAY_CHANGED` publishes a runtime
scale change. On Linux the available SDL video backend defines the result:
Wayland/high-DPI-capable backends expose the drawable/window ratio, while an
X11 configuration that reports identical logical and pixel sizes remains
identity rather than guessing from physical monitor DPI.

For fractional scaling, horizontal and vertical drawable/window ratios may
differ by one pixel. The SDL adapter averages them and rounds to the nearest
`1/64`; logical layout itself remains fractional, and only the existing
renderer-boundary policy snaps final physical edges. This keeps common
`1.25`, `1.5`, `1.75` and `2.0` scales stable across ordinary window resizes.

## Root policy and invalidation

Each document chooses `TC_UI_ROOT_LAYOUT_FULL_VIEWPORT` (the default) or
`TC_UI_ROOT_LAYOUT_SAFE_AREA`. The selected logical root rect is available
through `tc_ui_document_presentation_layout_rect`.

Changing metrics or root policy:

- advances the document-local presentation revision;
- invalidates layout and paint for every live widget;
- clears the previously cached layout rect.

Publishing an identical value is idempotent and does not advance the revision.
The painter/input integration is responsible for cancelling pointer state when
a changed physical/logical transform makes capture stale.

One live document instance has one metrics configuration at a time. A UI asset
shown simultaneously in presentations with different metrics must be
instantiated as separate documents. This avoids presentation-dependent widget
bounds being shared across viewports.

For scene documents rendered by `UIWidgetPass`, the pass is the presentation
owner of `physical_extent`: it replaces that field with the active render
target extent on every frame. A platform-published density scale, font scale,
and safe insets remain unchanged. If those insets are incompatible with the
new target extent, the submission is rejected with an error instead of being
silently converted to identity metrics. This split lets resizable editor
viewports reflow the same document while retaining Android density and
safe-area policy.

## Language projections

C uses the functions in `tc_ui_document.h`. C++ exposes the same value through
the non-owning `TcDocument` facade. Python exposes `PresentationMetrics`,
`PhysicalInsets`, `RootLayoutPolicy` and the corresponding `TcDocument`
properties. All projections preserve the same validation, revision and
ownership rules.
