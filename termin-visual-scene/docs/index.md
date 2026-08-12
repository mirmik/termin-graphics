# Termin Visual Scene

`termin-visual-scene` is a small retained 2D object tree. It owns graphic
items, composes their affine transforms, paints them into the canonical
`tgfx::DrawList2DBuilder`, performs geometric hit testing and provides
optional pointer/selection/drag controllers.

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
