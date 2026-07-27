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

## Immutable render preparation

`VisualScene2D::prepare_render_snapshot()` copies one coherent scene revision
under the scene mutex, releases that mutex, and only then calls the
host-supplied `SceneRenderResourceResolver2D`. The resolver maps persistent
font/image references and custom-batch keys to canonical `tgfx` runtime
handles and vertices. User resolver code therefore never runs under the scene
lock.

The result owns its item values and frozen `tgfx::DrawList2D`; it remains
structurally valid after the source scene changes or is destroyed. Runtime
font and texture handles are borrowed, however: the host's resource lease must
keep them live through `Canvas2DRenderer::execute()`. The snapshot stores no
device, pass, backend context or raw `FontAtlas*`.

Standard payloads lower directly to the single `tgfx::DrawCommand2D`
vocabulary. Effective affine transforms, opacity and all inherited clips are
preserved; clips are emitted as world-space geometry and never pre-reduced to
scissors. Missing resources or a rejected command abort the whole preparation
with an error log, so no partial snapshot is published.

## Hit testing and interaction

`hit_test()` evaluates front-to-back visual items using the prepared world
`Affine2f`, its fallible inverse, every inherited geometric clip and the
canonical `Path2f` fill/stroke predicates. Singular effective transforms are
diagnosed in item snapshots and are non-hittable; pointer movement does not
repeat the diagnostic or substitute identity. Descendants win over their
ancestors at equal z, while unrelated equal-z items use stable scene order.

`SceneInteraction2D` is an explicit controller rather than item policy. It
tracks hover, press and capture independently per pointer, auto-captures a
pressed target, emits semantic `ActionEvent2D` activation on a matching
release, and offers a host fallback for unclaimed plot/view input. Callbacks
run outside controller and scene locks. Reconciliation removes destroyed or
disabled targets and invalidates capture when topology changes.

`SelectionController2D` and `DragController2D` are reusable policies layered
over routed events. Dragging maps the world-space pointer delta through the
exact inverse parent transform and left-multiplies a parent-space translation,
so rotation, non-uniform scale and shear remain intact.

## Draggable primitive example

The supported example target is built with
`TERMIN_VISUAL_SCENE_BUILD_EXAMPLES=ON` (the repository default) and installed
in the SDK. Launch it after `./build-sdk.sh`:

```bash
./sdk/bin/termin_visual_scene_draggable_example
```

The window contains an overlapping rectangle, ellipse and diamond path. Move
the pointer to see hover feedback; press and drag any shape to exercise
selection and per-pointer capture; release to end the drag. Later-created,
higher-z items win overlap picking deterministically.

The same executable has a window-free CI mode:

```bash
./sdk/bin/termin_visual_scene_draggable_example --headless-smoke
```

It verifies public scene creation, canonical DrawList lowering and captured
dragging for all three primitives.
