# UnknownPass lifecycle contract

`UnknownPass` is the degraded representation of a frame pass whose runtime type
is temporarily unavailable during module unload or pipeline deserialization.
It is a real pipeline node, not a best-effort serialization fallback.

## Preserved state

The placeholder keeps:

- the original runtime type and inspect payload;
- the exact pipeline index and pass name;
- `enabled`, `passthrough`, viewport and debug-symbol state;
- reads, writes, in-place aliases, resource specs and internal symbols.

Execution is intentionally a no-op. Keeping the graph interface lets the
framegraph/editor retain topology and diagnostics while code is absent; it does
not pretend that the missing pass produced valid pixels.

## Module unload and restore

The frame-pass runtime facet has a prepare-unload callback. The engine replaces
all pipeline-owned instances with `UnknownPass` before unregistering the type.
If any original instances remain alive through external references, unload is
rejected: a live vtable may never outlive its module.

After a successful module load, placeholders whose `original_type` belongs to
that module are restored on unattached candidates using
`tc_inspect_deserialize_checked`. The pipeline slot is swapped only after the
entire payload is accepted. Schema drift or setter/conversion failure keeps the
placeholder and its original payload intact.

## Persistence and editor behavior

Pipeline serialization writes the original pass envelope, not `UnknownPass`.
The private `_unknown_graph` section carries the preserved graph contract so a
save/load cycle remains useful even while the module is absent. Deserializing an
unregistered pass type creates a placeholder instead of dropping the node or
failing the whole pipeline.

Python `TcPassRef` exposes `is_placeholder` and `original_type`. Framegraph
debugger output reports both the actual placeholder type and the unavailable
original type; the registry viewer labels degraded nodes explicitly.
