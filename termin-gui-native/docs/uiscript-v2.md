# Native UiScript v2

UiScript v2 is the native declarative authoring format for scene and tool UI.
Parsing, validation, materialization, named lookup and rollback are implemented
in `termin-gui-native`; Python only exposes bindings over those C++ objects.

```yaml
uiscript: 2
root:
  type: termin.gui.OverlayLayout
  name: hud
  children:
    - type: termin.gui.IconButton
      name: action
      icon: A
      size: 26
```

Widget types use their canonical runtime-registry names. A type is accepted
only when it has both a native widget factory and a native UiScript facet.
Unknown types, Python-only factories and unsupported properties fail with a
deterministic structural path before materialization.

The initial cross-platform baseline contains:

- `termin.gui.OverlayLayout`;
- `termin.gui.Panel`;
- `termin.gui.BoxLayout`;
- `termin.gui.HStack`;
- `termin.gui.VStack`;
- `termin.gui.GridLayout`;
- `termin.gui.ScrollArea`;
- `termin.gui.Label`;
- `termin.gui.WrapLayout`;
- `termin.gui.IconButton`.

Common properties are `visible`, `enabled`, and the normalized `layout`
mapping documented in [widget-layout-spec.md](widget-layout-spec.md). `anchor`
and `offset` describe placement in an `OverlayLayout`. Widget-specific
properties are declared by the registered type's UiScript facet.

An external widget facet may provide its own property validator alongside the
property-name list and materialization callback. Structural parsing invokes
that validator before storing the value, so extension modules do not need to
teach the central UiScript parser their property names or value types. Common
layout and visibility properties continue to use the core validators.

`BoxLayout`, `HStack`, and `VStack` accept `orientation` (`horizontal` or
`vertical`), non-negative `spacing`, `padding` as one number or
`[left, top, right, bottom]`, and `align_items` (`stretch`, `start`, `center`,
or `end`). A child of a Box may additionally declare:

```yaml
root:
  type: termin.gui.VStack
  padding: [12, 8, 12, 8]
  spacing: 6
  align_items: stretch
  children:
    - type: termin.gui.Panel
      basis: 48
    - type: termin.gui.Panel
      basis: preferred
      grow: 1
      shrink: 1
      min_extent: 80
      max_extent: 320
      align_self: center
```

`basis` is either a non-negative fixed primary-axis extent or `preferred`.
Without any placement fields, a child retains the historical Stretch policy:
preferred basis with grow and shrink weights of one. An explicit basis starts
with zero grow/shrink; weights can be supplied for a preferred basis.
`max_extent: 0` means unbounded. `align_self` also accepts `auto`, which uses
the container's `align_items`. Placement metadata belongs exclusively to the
parent Box facet; using these fields under another parent fails during
structural validation.

`GridLayout` declares non-empty `columns` and `rows` lists. Every track has a
`policy` (`fixed`, `preferred`, `flex`, or `stretch`). Fixed tracks require
`value`; flex tracks use a positive `value` as their default grow/shrink
weight. Non-fixed tracks may override `grow`, `shrink`, `min_extent`, and
`max_extent`; zero maximum remains unbounded. Grid children require
zero-based `row` and `column`, and may specify positive `row_span` and
`column_span`. Placements extending beyond the declared tracks are rejected
instead of implicitly creating tracks:

```yaml
root:
  type: termin.gui.GridLayout
  padding: 8
  column_spacing: 6
  row_spacing: 4
  columns:
    - policy: fixed
      value: 48
    - policy: flex
      value: 2
      min_extent: 80
      max_extent: 320
  rows:
    - policy: preferred
    - policy: stretch
  children:
    - type: termin.gui.Panel
      row: 0
      column: 0
      column_span: 2
```

`ScrollArea` accepts exactly zero or one declarative content child. The
`horizontal_scroll` and `vertical_scroll` booleans enable each scroll axis;
`horizontal_scrollbar` and `vertical_scrollbar` accept `auto`, `always`, or
`hidden`. Content measurement, viewport fitting on disabled axes, focus
reveal, and scroll clamping remain native `ScrollArea` behavior:

```yaml
root:
  type: termin.gui.ScrollArea
  horizontal_scroll: false
  vertical_scroll: true
  horizontal_scrollbar: hidden
  vertical_scrollbar: auto
  children:
    - type: termin.gui.VStack
      # ...
```

An immutable `UiScriptDescription` records the validated tree and its native
type dependencies. Materialization creates a fresh document tree for every
consumer. A failure destroys every widget created by that attempt. Reload
builds a complete replacement first and leaves the old tree alive on error.

## Responsive variants

Every node may contain validated `variants`. Selectors use the document's
logical viewport, never physical pixels or a platform name:

```yaml
root:
  type: termin.gui.BoxLayout
  orientation: vertical
  safe_area: ignore
  variants:
    - when: {width_class: compact}
      set: {padding: 8, spacing: 6}
    - when: {min_width: 600, orientation: landscape}
      priority: 10
      set: {orientation: horizontal, safe_area: respect}
```

Supported selector fields are `min_width`, `max_width`, `min_height`,
`max_height`, `orientation` (`portrait` or `landscape`), and `width_class`.
Minimum bounds are inclusive and maximum bounds are exclusive. Width classes
are `compact` below 600 logical units, `medium` from 600 through 839.999, and
`expanded` from 840. A square viewport is classified as landscape.

Matching variants are composed in ascending integer `priority`; source order
is retained for non-conflicting rules at the same priority. Two selectors
which can overlap at the same priority may not override the same property.
Such input is rejected as ambiguous and must assign distinct priorities.

The responsive override surface is intentionally closed:

- every widget may override `visible` and `layout`;
- Box/HStack/VStack may override `orientation`, `spacing`, and `padding`;
- a child of GridLayout may override `row`, `column`, `row_span`, and
  `column_span`;
- the root may override `safe_area` (`respect` or `ignore`).

The base root may also declare `safe_area`. Rules are evaluated immediately
before each document layout pass and cached until the matching set changes.
They update existing widget handles and parent placement metadata; they never
rematerialize the tree. Making a subtree invisible uses the normal widget
participation path, which removes it from paint, hit testing, focus, and
pointer capture. Compiled UI document assets preserve selectors, priorities,
and overrides exactly.

## UI document assets

`UiDocumentAsset` is the durable native asset wrapper around a validated
description. Assets are registered by UUID and referenced through
generation-checked `UiDocumentAssetHandle` values. The asset payload is
immutable: a source reload parses and validates a complete replacement before
atomically replacing the registry entry and incrementing its revision. Live
widget trees are independent instances and are replaced separately with
`reload_instance`.

Runtime packages store the normalized native recipe as
`ui/<uuid>.ui-document.json` and list it as a `ui_document` resource. The
compiled payload uses `ui_document_asset: 1`, records source identity and
revision, and includes the exact canonical widget-type dependency list.
Runtime package loading validates and registers these assets before scene
deserialization, without importing Python.

The registry operation itself is safe to stage before a frame boundary. The
engine-level hot-reload coordinator is expected to publish staged asset and
instance replacements transactionally while rendering is paused between
frames; that coordinator is outside this asset layer.

UiScript is deliberately separate from universal durable document
serialization. Declarative-only widgets fail the generic persistence API until
they receive an explicit state codec under the broader persistence contract.

Version 1 was removed during the native scene-UI migration. Repository assets
were migrated explicitly; there is no compatibility fallback.
