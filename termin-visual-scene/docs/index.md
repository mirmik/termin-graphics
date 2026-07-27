# Termin Visual Scene

`termin-visual-scene` owns retained 2D item identity, topology and interaction.
It depends on the canonical geometry and drawing vocabulary in `termin-base`
and `termin-graphics`; those lower-level modules do not depend on it.

The C storage API uses per-scene index/generation handles. A handle includes
the scene lifetime-domain ID, so stale and cross-scene references are rejected.
Adopted payloads are destroyed exactly once by their optional custom deleter.
The C++ facade is move-only and does not use shared ownership for items.

All operations on a live scene are internally synchronized. A topology view is
a momentary snapshot and its payload pointer is borrowed; callers must provide
higher-level synchronization if they keep that pointer across mutations.
Destroying a scene requires exclusive lifetime ownership: no operation may race
with `tc_visual_scene_destroy`, and a payload deleter must not destroy its
owning scene recursively.

## Typed retained items

`VisualScene2D` layers inspectable retained state over the generation-handle
storage. Each item has an exact local `termin::Affine2f`, visibility, opacity,
stable z/order, an optional geometric `tgfx::Path2f` clip, a dirty revision,
and one canonical payload variant. Payloads cover groups, rectangles, rounded
rectangles, ellipses, paths, polylines, text, images, hit regions and custom
batch references.

Text and image items keep only serializable `StableResourceRef2D` values.
Runtime font and texture handles are deliberately absent from persistent scene
state and are resolved when an immutable render snapshot is prepared. Custom
batches likewise keep a stable key and bounds rather than large vertex arrays
or a GPU allocation.

Snapshots compose the full affine matrix without TRS decomposition, multiply
inherited opacity and visibility, and retain every inherited clip as transformed
geometry. `Rect2f` remains local origin/extent geometry; `Bounds2f` is the
axis-aligned local or transformed result. Equal-z snapshots are ordered by a
monotonic scene-owned insertion order.

All typed mutations validate before committing under the scene mutex. A failed
path, paint, resource, transform, opacity or geometry update is logged and
leaves the previous payload, state and revision unchanged.
