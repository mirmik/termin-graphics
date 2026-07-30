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
- `termin.gui.HStack`;
- `termin.gui.VStack`;
- `termin.gui.IconButton`.

Common properties are `visible` and `enabled`. `anchor` and `offset` describe
placement in an `OverlayLayout`. Widget-specific properties are declared by
the registered type's UiScript facet.

An immutable `UiScriptDescription` records the validated tree and its native
type dependencies. Materialization creates a fresh document tree for every
consumer. A failure destroys every widget created by that attempt. Reload
builds a complete replacement first and leaves the old tree alive on error.

UiScript is deliberately separate from universal durable document
serialization. Declarative-only widgets fail the generic persistence API until
they receive an explicit state codec under the broader persistence contract.

Version 1 was removed during the native scene-UI migration. Repository assets
were migrated explicitly; there is no compatibility fallback.
