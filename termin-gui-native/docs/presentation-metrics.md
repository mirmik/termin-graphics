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

## Language projections

C uses the functions in `tc_ui_document.h`. C++ exposes the same value through
the non-owning `TcDocument` facade. Python exposes `PresentationMetrics`,
`PhysicalInsets`, `RootLayoutPolicy` and the corresponding `TcDocument`
properties. All projections preserve the same validation, revision and
ownership rules.
