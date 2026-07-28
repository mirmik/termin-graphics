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

Traversal sorts roots and siblings by z-order and stable order, pushes each
item's local transform, opacity and optional geometric clip, then calls the
item paint vtable. The item emits canonical draw commands through
`GraphicItemPaintContext2D`; the scene renderer never branches on its concrete
type.

Text, image and custom-batch items resolve their runtime resources
synchronously during this traversal. The scene does not create a detached
render snapshot, retain a render context or defer item callbacks. A host may
freeze or execute the builder according to the surrounding render pipeline.

## Hit testing and interaction

`hit_test` traverses the same live visual tree front-to-back. It composes the
exact affine hierarchy, rejects singular inverse transforms, checks inherited
geometric clips and calls the item's hit-test vtable in local coordinates.
Children win over their parent at the same visual level.

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

Serialization, detached inspection, state RPC and scene snapshots are not
responsibilities of this module. A domain that needs a serializable document
or immutable data snapshot owns that representation above the visual scene.

## Example

After building the SDK:

```bash
./sdk/bin/termin_visual_scene_draggable_example
./sdk/bin/termin_visual_scene_draggable_example --headless-smoke
```

The example creates three ordinary item objects, paints them through direct
scene traversal and exercises hit testing, capture, selection and dragging.
