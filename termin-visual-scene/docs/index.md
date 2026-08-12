# Termin Visual Scene

`termin-visual-scene` provides small retained visual object trees. The mature
2D path owns graphic items, composes their affine transforms, paints them into
the canonical `tgfx::DrawList2DBuilder`, performs geometric hit testing and
provides optional pointer/selection/drag controllers. `VisualScene3D` follows
the same deliberately small model for embedded 3D tools and previews; it is
not an ECS or a replacement for `tc_scene`.

## VisualScene3D direction

VisualScene3D is intended for a handful of heterogeneous visual items such as
plot parts, reconstruction meshes, point clouds, editor handles and an
orientation cube. Concrete items own their paint and ray-hit behavior. The
scene performs a linear traversal, asks every eligible item for a candidate
when interaction needs one and chooses the nearest valid candidate. It does
not maintain a BVH, require item AABBs as a broad phase, use a GPU ID pass or
know about gizmos and other concrete item types.

The 3D storage is dimension-specific rather than adding conditional 3D fields
to the existing 2D item ABI. It retains the established ownership model:
creator-supplied deleters, one scene owner, generation-checked external
handles, pointer-based parent/child topology and deterministic replacement.

Local placement is stored as exact double-precision `Affine3d`. In
particular, parent rotation composed with non-uniform child scale must not be
projected back into a TRS value. Concrete items and applications may expose
convenient position/rotation/scale properties, but those are adapters above
the scene's exact affine composition contract.

Public transform setters reject non-finite coefficients and log the failure.
Singular but finite transforms remain valid placement state: they can still be
painted and inspected, while later hit-testing skips items whose world affine
cannot map a ray into local space and records that diagnostic.

Hit testing is item-owned. A host constructs a world ray from its camera and
viewport, then the scene linearly visits every effectively visible and enabled
item that implements `hit_test`. The scene normalizes the world direction and
maps the ray through the exact inverse world affine. It does not renormalize
the resulting local direction, so an item-local ray parameter remains a world
distance even under non-uniform scale. The nearest positive finite candidate
wins; exact ties retain stable adoption order. An optional item-provided local
bounds callback exists for fit and inspection, never as a required broad
phase. Item hit callbacks are synchronous borrowed calls and must not mutate
scene topology; a detected topology mutation aborts the query with a log.

`SceneInteraction3D` consumes these ready world-ray pointer events and keeps
hovered, pressed and captured generation handles. Capture continues when the
ray leaves an item's hit region, stale or newly ineligible handles reconcile
before every route, and unhandled events go to a host-installed fallback such
as camera navigation. Action identity includes the item-defined `part` token,
allowing one ordinary item to expose gizmo axes or orientation-cube faces
without teaching the scene either concept.

Interactive behavior is registered externally per item. Target handlers
receive ordered enter/leave/down/move/up/cancel events. The pressed `part` and
hit record are retained with capture, so a controller can continue an axis or
handle drag after the pointer leaves its geometry. Hiding, disabling,
destroying, replacing or explicitly cancelling a scene target produces the
same deterministic leave/cancel lifecycle before state is discarded. These
handlers are controllers owned by the host or tool; they are not methods on
`VisualItem3D` and do not add editor semantics to the scene.

Camera ownership is also outside the scene. A GUI view or another host
supplies view/projection state, maps pointer positions to world rays and may
install camera navigation as the generic unhandled-event fallback. This lets
one VisualScene3D be displayed by multiple views and lets a small 3D scene be
embedded as an ordinary native GUI widget.

3D painting is likewise item-owned. `tc_visual_scene3d_paint` synchronously
visits visible items in stable scene order and supplies each callback with its
exact `world_from_local` affine, effective visibility/enabled state and the
caller-owned `tc_visual_view3d`. Items submit borrowed draw packets identified
by protocol strings; renderer-specific sinks interpret them. The scene never
branches on meshes, point clouds, gizmos or annotations.

The draw sink is transactional. It stages packets after `begin`, publishes the
complete batch only when `end` succeeds and discards staged work on `abort`.
Item failure, packet rejection, topology mutation during a callback or sink
failure aborts traversal and is logged. All view, packet and submission pointers
are borrowed only for the current callback; a sink must copy data or retain
resources according to its packet protocol before `submit` returns. Invisible
ancestors skip their subtree. Disabled items still paint with
`effective_enabled = false`, allowing the consumer to choose their appearance.

The C++ `ScenePaintSink3D` in `paint3d.hpp` is the adapter seam for an external
`RenderItemSource` or immediate-renderer collector. Camera projection and
render-engine integration remain outside this package; `termin-visual-scene`
does not depend on `tc_scene` or `termin-render`.

### Built-in 3D items

The native C++ layer provides four ordinary implementations of the same public
item contract:

- `GroupItem3D` is empty placement/topology;
- `PrimitiveItem3D` owns a shared colored indexed-triangle batch with an
  optional part token per triangle, suitable for tool handles and orientation
  geometry;
- `StaticMeshItem3D` owns a shared CPU `Mesh3` for reconstruction and preview
  surfaces. It may additionally own an immutable RGBA8 sRGB base-color
  texture snapshot; `tint` is multiplied as its linear base-color factor;
- `PointCloudItem3D` owns shared point data, draw style and a local-space pick
  radius. Point hits report `point_index + 1` as their part token.

These classes derive from `NativeVisualItem3D`, which translates C++ virtual
callbacks to the C item vtable and logs exceptions before they cross the ABI.
The scene contains no dispatch for any built-in type.

Geometry resources passed to an item are immutable shared resources. Callers
must not mutate the original object after sharing it. Replacing a resource
validates the complete replacement before changing the item; completed paint
batches may retain their own `shared_ptr` after the item is replaced or
destroyed. Built-in draw packet structures therefore contain non-trivial C++
objects: a protocol adapter must cast the borrowed packet to its declared type
and copy-construct the packet or its resource handle. It must never preserve or
byte-copy the raw payload storage after `submit` returns.

The module is deliberately modeled after the native widget object model:

- every implementation embeds one `tc_graphic_item` C base;
- type-specific behavior is dispatched through one vtable;
- `tc_visual_scene` adopts the object with its creator-supplied deleter;
- parent and ordered children are direct object pointers;
- generation handles are non-owning external references;
- destruction invalidates the handle and destroys the whole child subtree.

There is no parallel record model, concrete-type sum, registry dispatch,
visitor or renderer knowledge of built-in item classes. `GroupItem2D`,
`RectItem2D`, `PathItem2D`, `TextItem2D` and the other built-ins are ordinary
implementations of the same virtual contract available to custom items.

## Ownership and threading

A scene is the only owner of every adopted item. C++ callers normally transfer
a `std::unique_ptr<GraphicItem2D>` to `TcVisualScene::adopt`; C and language
bindings pass an embedded base plus exactly one deleter. Item references do not
keep either the item or scene alive. A stale or cross-scene generation handle
does not resolve.

The scene is thread-confined and contains no mutex. Calls on one scene must not
overlap. If visual scenes later need cross-thread mutation or rendering, that
contract will be designed separately.

## State and topology

`tc_graphic_item` stores the shared state:

- local `Affine2f`;
- visibility, enabled state and opacity;
- sibling z-order and stable adoption order;
- parent and ordered children;
- language body, vtable, deleter and runtime-type link.

Concrete objects store their own geometry, paint and resource references.
`GraphicItem2D` exposes ordinary setters and child operations. The scene
computes world transform, effective visibility/enabled/opacity, local subtree
bounds and world bounds directly from the live tree.

`TcVisualScene::replace` exists for projections that must retain an external
handle while changing their concrete implementation. It preserves identity,
topology and common placement state and destroys the old object exactly once.

## Painting

Painting is immediate scene traversal:

```cpp
tgfx::DrawList2DBuilder builder;
if (!scene.paint(builder, resources)) {
    // The item or resource resolver logged the failure.
}
```

Traversal orders roots and siblings by z-order and stable order. That ordered
tree is cached until adopt/replace/destroy/reparent/z-order changes the scene's
order revision; content, geometry and data mutations do not rebuild it.
Identity transforms and unit opacity do not emit redundant state commands.
Traversal feeds each item's affine transform, opacity, visibility and geometric
clip into `tgfx::CompositionEvaluator2D`, which owns the balanced draw-list
scopes. The item paint vtable emits canonical draw commands and nested local
clip scopes through `GraphicItemPaintContext2D`; the scene renderer never
branches on its concrete type.

Cross-tree compositors can instead call `paint_layers`. It emits one balanced,
self-contained `DrawList2D` for each item's own paint slot in the identical
tree order. A sink may interleave foreign content between these layers without
adding that foreign semantic type to visual-scene. `visit_hit_layers` exposes
the exact reverse traversal with evaluated clips and local point mapping, so
paint and input adapters share one stacking contract.

Text, image and custom-batch items resolve their runtime resources
synchronously during this traversal. The scene does not create a detached
render snapshot, retain a render context or defer item callbacks. A host may
freeze or execute the builder according to the surrounding render pipeline.

## Hit testing and interaction

`local_bounds`, `world_bounds` and `hit_test` use the same shared composition
evaluator as painting. Bounds preserve arbitrary-affine projection. Hit testing
traverses the live tree front-to-back, rejects singular inverse transforms,
checks every inherited geometric clip as a path rather than an AABB, and calls
the item's hit-test vtable in evaluator-mapped local coordinates. Children win
over their parent at the same visual level.

`SceneInteraction2D` stores hover, press and capture as generation handles.
`SelectionController2D` and `DragController2D` are optional policies rather
than behavior embedded in items. Detaching an item merely makes it a root and
does not invalidate identity; disabling or destroying it reconciles active
interaction state.

## Bindings

Python `GraphicItemRef2D` is a thin scene-lifetime-plus-handle reference.
Common properties read and mutate the live object. Explicit destruction
invalidates the reference. GUI-native does not define a second graphic-item
reference or scene wrapper: `SceneView` accepts the same shared
`TcVisualScene` directly.

Python 3D bindings follow the identical explicit-lifetime model.
`tc_visual_scene3d_create` returns the canonical `TcVisualScene3D` facade and
`tc_visual_scene3d_destroy` invalidates every `VisualItemRef3D` derived from
it. Generic topology, exact `Affine3d` placement, bounds and ray hits operate
through generation handles. Primitive, static-mesh and point-cloud factories
return typed refs with their concrete mutation surface; Python input is copied
into an immutable native resource snapshot so later mutation of an authored
`Mesh3` or input container cannot desynchronize paint, bounds and hit testing.

`SceneInteraction3D` accepts caller-projected `Ray3` pointer events and returns
dispatch records containing live item refs. Python action and fallback callback
exceptions cross no native boundary: the C++ interaction layer logs them and
sets `PointerDispatch3D.callback_failed`. Arbitrary Python-owned item bodies are
intentionally not exposed; their callback threading, lifetime and failure
contract remains separate future work.

The public C boundary in `tc_visual_scene_item2d.h` exposes common
generation-handle operations for topology, transforms, presentation, bounds
and clips. `tc_builtin_items2d.h` provides concrete factories and mutations
for native Group, Rect, Path, Text, Image and HitRegion items. Factories belong
to those concrete types; there is no closed scene-level type enumeration.

`Termin.Native` mirrors this contract with non-owning typed
`GraphicItemRef2D` wrappers. `TcVisualScene2D` explicitly owns the native
scene; disposing it invalidates all existing item wrappers. Built-in bodies,
painting and resource access remain native. User-defined language-owned item
bodies are intentionally deferred to task `#1107`.

Serialization, detached inspection, state RPC and scene snapshots are not
responsibilities of this module. A domain that needs a serializable document
or immutable data snapshot owns that representation above the visual scene.

## Draggable primitive example

The supported example target is built with
`TERMIN_VISUAL_SCENE_BUILD_EXAMPLES=ON` (the repository default) and installed
in the SDK. Launch it after `./build-sdk.sh`:

```bash
./sdk/bin/termin_visual_scene_draggable_example
```

The standalone host discovers `termin_shaderc` and `slangc` from explicit
`TERMIN_SHADERC` / `TERMIN_SLANGC` settings, the active SDK, or `PATH`.
Generated Vulkan/D3D11 shader artifacts are kept in the platform user cache;
set `TERMIN_SDK_SHADER_CACHE_ROOT` to override its base directory.

The example creates three ordinary item objects—an overlapping rectangle,
ellipse and diamond path—and paints them through direct scene traversal. Move
the pointer to see hover feedback; press and drag any shape to exercise
selection and per-pointer capture; release to end the drag. Later-created,
higher-z items win overlap picking deterministically.

The same executable has a window-free CI mode:

```bash
./sdk/bin/termin_visual_scene_draggable_example --headless-smoke
```

It verifies public scene creation, canonical DrawList lowering and captured
dragging for all three primitives.

The no-window GPU path used by CI is also available directly:

```bash
./sdk/bin/termin_visual_scene_draggable_example --shader-smoke
```

It creates an isolated Vulkan or D3D11 device, configures the standalone shader
runtime and executes the example's actual Canvas2D draw list.
