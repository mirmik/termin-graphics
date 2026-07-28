#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include <cmath>

#include "termin_visual_scene/interaction2d.hpp"

namespace {

using namespace termin::visual;

tgfx::Path2f box(float x, float y, float width, float height) {
    tgfx::Path2f path;
    assert(path.move_to({x, y}));
    assert(path.line_to({x + width, y}));
    assert(path.line_to({x + width, y + height}));
    assert(path.line_to({x, y + height}));
    assert(path.close());
    return path;
}

bool same(GraphicItemHandle a, GraphicItemHandle b) {
    return a.scene_id == b.scene_id &&
           a.index == b.index &&
           a.generation == b.generation;
}

bool near(float a, float b) {
    return std::abs(a - b) < 1e-4f;
}

}  // namespace

int main() {
    VisualScene2D scene;
    const auto parent = scene.create(RectItem2D{
        {0.0f, 0.0f, 40.0f, 40.0f}, {}, std::nullopt});
    assert(parent);
    GraphicItemState2D parent_state;
    parent_state.local_transform =
        termin::Affine2f::translation(30.0f, 20.0f) *
        termin::Affine2f::shear(0.4f, -0.2f);
    parent_state.clip = GeometricClip2D{
        box(0.0f, 0.0f, 30.0f, 30.0f), tgfx::FillRule::NonZero};
    parent_state.z_order = 3;
    assert(scene.set_state(*parent, parent_state));

    const auto child = scene.create(EllipseItem2D{
        {0.0f, 0.0f, 20.0f, 20.0f}, {}, std::nullopt}, *parent);
    assert(child);
    GraphicItemState2D child_state;
    child_state.local_transform =
        termin::Affine2f::rotation(0.35f) *
        termin::Affine2f::scaling(1.5f, 0.75f);
    child_state.clip = GeometricClip2D{
        box(0.0f, 0.0f, 15.0f, 20.0f), tgfx::FillRule::EvenOdd};
    child_state.z_order = 3;
    assert(scene.set_state(*child, child_state));

    const auto child_snapshot = scene.snapshot(*child);
    const auto inside_world =
        child_snapshot->world_transform.transform_point({10.0f, 10.0f});
    const auto picked = hit_test(scene, inside_world);
    assert(picked && same(*picked, *child));  // deepest target beats its parent

    const auto clipped_world =
        child_snapshot->world_transform.transform_point({18.0f, 10.0f});
    const auto clipped_pick = hit_test(scene, clipped_world);
    assert(!clipped_pick || !same(*clipped_pick, *child));

    // Equal-z unrelated overlap follows stable scene order.
    const auto back = scene.create(RectItem2D{
        {100.0f, 0.0f, 20.0f, 20.0f}, {}, std::nullopt});
    const auto front = scene.create(PathItem2D{
        box(100.0f, 0.0f, 20.0f, 20.0f),
        tgfx::FillPaint{},
        std::nullopt});
    assert(back && front);
    GraphicItemState2D equal_z;
    equal_z.z_order = 9;
    assert(scene.set_state(*back, equal_z));
    assert(scene.set_state(*front, equal_z));
    const auto overlap = hit_test(scene, {110.0f, 10.0f});
    assert(overlap && same(*overlap, *front));

    // Singular transforms are diagnosed during scene preparation and are
    // deterministically non-hittable without an identity fallback.
    auto singular = equal_z;
    singular.z_order = 20;
    singular.local_transform = termin::Affine2f::scaling(0.0f, 1.0f);
    assert(scene.set_state(*front, singular));
    assert(scene.snapshot(*front)->diagnostics ==
           GraphicItemDiagnostic2D::SingularWorldTransform);
    const auto after_singular = hit_test(scene, {110.0f, 10.0f});
    assert(after_singular && same(*after_singular, *back));

    SceneInteraction2D interaction;
    SelectionController2D selection;
    DragController2D drag;
    int actions = 0;
    int fallbacks = 0;
    interaction.set_action_handler(*child, [&](const ActionEvent2D& event) {
        assert(same(event.target, *child));
        ++actions;
    });
    interaction.set_fallback_handler([&](const PointerEvent2D&) {
        ++fallbacks;
    });

    auto down = interaction.route(scene, {
        1, PointerEventKind2D::Down, inside_world, 0});
    assert(same(down.target, *child));
    assert(same(down.captured, *child));
    selection.handle(scene, down);
    assert(selection.selection().size() == 1);
    assert(drag.handle(scene, down));

    // A second pointer owns independent hover/press/capture state.
    auto second_down = interaction.route(scene, {
        2, PointerEventKind2D::Down, {105.0f, 5.0f}, 0});
    assert(same(second_down.target, *back));
    assert(same(interaction.captured(1), *child));
    assert(same(interaction.captured(2), *back));
    const auto second_outside = interaction.route(scene, {
        2, PointerEventKind2D::Move, {-500.0f, -500.0f}, 0});
    assert(same(second_outside.target, *back));

    const termin::Vec2f delta{8.0f, -3.0f};
    auto move = interaction.route(scene, {
        1,
        PointerEventKind2D::Move,
        {inside_world.x + delta.x, inside_world.y + delta.y},
        0,
    });
    assert(same(move.target, *child));
    assert(drag.handle(scene, move));
    const auto moved = scene.snapshot(*child);
    const auto moved_world =
        moved->world_transform.transform_point({10.0f, 10.0f});
    assert(near(moved_world.x, inside_world.x + delta.x));
    assert(near(moved_world.y, inside_world.y + delta.y));

    // Release inside the moved target delivers one semantic action.
    auto up = interaction.route(scene, {
        1, PointerEventKind2D::Up, moved_world, 0});
    assert(same(up.target, *child));
    assert(up.action);
    assert(actions == 1);
    assert(tc_graphic_item_handle_is_invalid(interaction.captured(1)));
    drag.handle(scene, up);

    // Capture is invalidated by topology change, disabling and destruction.
    assert(interaction.capture(scene, 3, *child));
    assert(scene.detach(*child));
    auto after_detach = interaction.route(scene, {
        3, PointerEventKind2D::Move, moved_world, 0});
    assert(tc_graphic_item_handle_is_invalid(after_detach.captured));
    selection.reconcile(scene);
    assert(selection.selection().empty());

    assert(interaction.capture(scene, 4, *back));
    auto disabled = scene.snapshot(*back)->state;
    disabled.enabled = false;
    assert(scene.set_state(*back, disabled));
    interaction.route(scene, {
        4, PointerEventKind2D::Move, {105.0f, 5.0f}, 0});
    assert(tc_graphic_item_handle_is_invalid(interaction.captured(4)));

    disabled.enabled = true;
    assert(scene.set_state(*back, disabled));
    assert(interaction.capture(scene, 5, *back));
    assert(scene.destroy_leaf(*back));
    interaction.route(scene, {
        5, PointerEventKind2D::Move, {105.0f, 5.0f}, 0});
    assert(tc_graphic_item_handle_is_invalid(interaction.captured(5)));

    // Unclaimed space reaches the plot/host fallback path.
    auto fallback = interaction.route(scene, {
        7, PointerEventKind2D::Down, {-1000.0f, -1000.0f}, 0});
    assert(fallback.used_fallback);
    assert(fallbacks >= 1);

    const auto detached_world =
        scene.snapshot(*child)->world_transform.transform_point({10.0f, 10.0f});
    selection.handle(scene, interaction.route(scene, {
        8, PointerEventKind2D::Down, detached_world, 0}));
    assert(selection.selection().size() == 1);
    assert(scene.destroy_leaf(*child));
    selection.reconcile(scene);
    assert(selection.selection().empty());
}
