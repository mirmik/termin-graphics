# Termin Visual Scene

`termin-visual-scene` owns retained 2D item identity, topology and interaction.
It depends on the canonical geometry and drawing vocabulary in `termin-base`
and `termin-graphics`; those lower-level modules do not depend on it.

The C storage API uses per-scene index/generation handles. A handle includes
the scene lifetime-domain ID, so stale and cross-scene references are rejected.
Each language implementation embeds the common `tc_graphic_item` C base and
supplies its vtable, native language, body and creator-owned deleter.
`tc_visual_scene_adopt` transfers that item to one scene and requires a
non-null deleter. Failed adoption rolls ownership back through the same
deleter; explicit item destruction and scene teardown invoke `on_destroy` and
the deleter exactly once after invalidating the generation handle. The C++
facade is move-only and does not use shared ownership for items.

All operations on a live scene are internally synchronized. A topology view is
a momentary snapshot and its `tc_graphic_item*` is borrowed; callers must provide
higher-level synchronization if they keep that pointer across mutations.
Destroying a scene requires exclusive lifetime ownership: no operation may race
with `tc_visual_scene_destroy`, and an item deleter must not destroy its
owning scene recursively.

Lifecycle callbacks and deleters run outside the storage mutex. The common
state API validates and mutates local `Affine2f`, visibility, enabled state,
opacity and z-order under that mutex; direct writes to an attached base are not
a synchronized mutation path. Topology and dirty revisions live in the common
item object rather than a parallel slot payload.

## Typed retained items

`VisualScene2D` layers inspectable retained behavior over generation-handle
storage without a second scene tree. Every built-in is a registered concrete
C++ body embedding `tc_graphic_item`; ownership, identity, topology, common
state, stable order and revisions live in that canonical object. Bounds,
immutable snapshot emission and hit preparation are type vtable operations.
Payloads cover groups, rectangles, rounded rectangles, ellipses, paths,
polylines, text, images, hit regions and custom batch references.

`GraphicItemPayload2D` remains as a copied detached value used by the current
C++ snapshot and serialization API. It is not retained canonical storage and
is not the target cross-language extension mechanism.

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

## Inspection and persistence

`VisualScene2D::inspection()` returns a fully detached `SceneInspection2D`.
Items use document-local record indices and separate monotonic `stable_id`
values, never runtime slot/generation handles. The snapshot includes canonical
payload type names, parent/ordered-child topology, local and effective
transforms and flags, bounds, diagnostics and revisions. It remains valid
after later mutation or destruction of the scene.

`serialize()` emits the versioned `termin.visual_scene.2d` schema as
`tc::trent`. It persists record topology, stable IDs, item state and all
standard payload variants, including complete path and paint data. Runtime
handles and transient hover, press, capture and dirty controller state are not
part of the schema.

`restore()` accepts only a supported schema and an empty destination scene. It
parses and validates the complete document, builds a private staging scene and
commits it atomically. Unknown type names, malformed topology or invalid
payloads are logged and leave the destination empty and unchanged.

## Python bindings

`termin.visual_scene.VisualScene2D` is an owning Python document wrapper.
Creation returns `GraphicItemRef2D` values containing only a shared
invalidation token and the native scene/index/generation handle. An item
reference does not own its item and does not keep the scene alive; after item
destruction or scene collection, `valid` becomes false and operations raise
`ReferenceError`.

The wrapper exposes retained primitive creation, state mutation, destruction,
detached snapshots and inspection, plus the same versioned JSON persistence
contract as the C++ API. A parent reference from another scene is rejected
instead of being interpreted as a local slot.

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
