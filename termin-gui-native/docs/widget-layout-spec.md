# Generic widget layout spec

Every `tc_widget` owns one platform-neutral `tc_ui_widget_layout_spec`. Its
default is fully automatic and therefore does not change the existing
`min_size`, `preferred_size`, `max_size`, measurement, or bounds behavior.
Container implementations consume the spec; the widget never reads display
density or platform state.

Width and height accept:

- `auto`, which uses intrinsic measurement;
- a non-negative fixed logical size;
- `fill`, which uses a definite parent content extent;
- a percentage in the `[0, 1]` range.

`fill` and percentage resolve as `auto` while their parent axis is indefinite.
This is the cycle-breaking rule for intrinsic measurement. Once a container
has a definite content extent, it can call
`tc_ui_widget_layout_spec_resolve_size` again for constrained layout.

Minimum and maximum width/height are finite non-negative logical values. Zero
maximum means unbounded. A non-zero maximum below its minimum is invalid.
Margins are ordered left, top, right, bottom and remain parent-consumed
external space.

An aspect ratio is either zero (absent) or positive. It may accompany at most
one non-automatic axis: that axis determines the other. Specifying width,
height, and an aspect ratio together is rejected as over-constrained.

The optional `TC_UI_TOUCH_TARGET_LAYOUT_MINIMUM` policy expands the resolved
layout size to the explicit logical `minimum_touch_target`. It does not alter
paint or hit testing behind a container's back. Flex basis/grow/shrink,
primary-axis extent limits, and align-self are child-placement state owned by
the parent container and are intentionally absent from this common value.
A finite maximum smaller than the requested touch minimum is rejected, so the
policy cannot silently promise a target that layout then clamps away.

`tc_ui_widget_layout_spec_normalize` validates and canonicalizes a value.
Widget setters are transactional: an invalid value is logged and leaves the
previous spec unchanged. Snapshots and document serialization contain the
normalized value. C++ exposes it through `Widget::layout_spec`; Python uses
`WidgetLayoutSpec`, `LayoutLength`, `LengthMode`, and `TouchTargetPolicy`.

UiScript uses one common `layout` mapping:

```yaml
layout:
  width: 50%
  height: fill
  min_width: 120
  max_width: 480
  margin: [8, 12, 8, 12]
  aspect_ratio: 1.7777778
  minimum_touch_target: [48, 44]
```

A fixed length is a number. `auto`, `fill`, and percentage strings are the
other accepted length forms. Margin accepts either one non-negative number or
`[left, top, right, bottom]`. A minimum touch target accepts `false`, one size,
or `[width, height]`; `true` is rejected because it has no deterministic size.
Unknown fields and invalid combinations fail parsing before materialization,
so reload remains transactional.
