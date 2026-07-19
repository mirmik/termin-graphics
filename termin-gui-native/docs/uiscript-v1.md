# gui-native uiscript v1

`termin.gui_native.UiScriptLoader` reads the explicitly versioned YAML dialect
identified by `uiscript: 1`. Parsing produces a toolkit-neutral
`UiScriptDescription`; native widgets are created only after the entire source
has validated.

Version 1 intentionally is not the complete legacy `tcgui` DSL. It supports
`Overlay`, `Panel`, `HStack`, `VStack`, and `IconButton`, with the properties used by the
editor camera overlay. Unknown document keys, widget types, properties,
anchors, malformed values, and duplicate names are errors with a structural
source path such as `root.children[1].size`.

```yaml
uiscript: 1
root:
  type: Overlay
  name: overlay
  background_color: [0, 0, 0, 0]
  anchor: absolute
  position: [0px, 0px]
  width: 100%
  height: 100%
  children:
    - type: HStack
      spacing: 4
      children:
        - type: IconButton
          name: inspect
          icon: "I"
          tooltip: "Inspect"
          size: 26
```

Names are unique within a document. Materialization copies each name to the
native widget `name` and `stable_id`, and `loaded.named("inspect")` returns the
typed native wrapper suitable for controller binding. Declarative properties
remain available through `loaded.widgets[name].properties`; composition
properties (`anchor`, `offset`, `position`, `width`, and `height`) are consumed
by the viewport overlay composition layer.

`Overlay` is the native stacking container. Children are painted in declaration
order and hit-tested in reverse order, so later children are visually and
interactively above earlier ones. `absolute` (or an omitted anchor) fills the
overlay; corner anchors use the child's measured size and apply `offset` from
that corner. The overlay itself is mouse-transparent, allowing an uncovered
point to reach a lower sibling such as `Viewport3D`. Layout is recomputed from
the current parent rectangle after every resize.

`LoadedUiScript.close()` recursively destroys the owned root. Reload first
builds a complete replacement tree in the same `Document`, then destroys the
old tree. A parse, factory, property, or parenting failure destroys everything
created by that attempt and leaves the previous loaded tree alive.
