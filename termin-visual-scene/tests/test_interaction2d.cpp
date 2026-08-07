#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include <cmath>
#include <memory>

#include "termin_visual_scene/builtin_items2d.hpp"
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
        return a.scene_id == b.scene_id && a.index == b.index && a.generation == b.generation;
    }

    bool near(float a, float b) {
        return std::abs(a - b) < 1e-4f;
    }

} // namespace

int main() {
    const auto scene_handle = tc_visual_scene_create();
    TcVisualScene scene{scene_handle};
    auto parent_object =
        std::make_unique<RectItem2D>(termin::Rect2f{0.0f, 0.0f, 40.0f, 40.0f}, tgfx::FillPaint{}, std::nullopt);
    RectItem2D* parent_ptr = parent_object.get();
    const auto parent = scene.adopt(std::move(parent_object));
    assert(parent);
    const auto parent_transform = termin::Affine2f::translation(30.0f, 20.0f) * termin::Affine2f::shear(0.4f, -0.2f);
    parent_ptr->set_local_transform(parent_transform);
    parent_ptr->set_clip(GeometricClip2D{box(0.0f, 0.0f, 30.0f, 30.0f), tgfx::FillRule::NonZero});
    parent_ptr->set_z_order(3);

    auto child_object =
        std::make_unique<EllipseItem2D>(termin::Rect2f{0.0f, 0.0f, 20.0f, 20.0f}, tgfx::FillPaint{}, std::nullopt);
    EllipseItem2D* child_ptr = child_object.get();
    const auto child = scene.adopt(std::move(child_object), parent_ptr);
    assert(child);
    child_ptr->set_local_transform(termin::Affine2f::rotation(0.35f) * termin::Affine2f::scaling(1.5f, 0.75f));
    child_ptr->set_clip(GeometricClip2D{box(0.0f, 0.0f, 15.0f, 20.0f), tgfx::FillRule::EvenOdd});
    child_ptr->set_z_order(3);

    const auto inside_world = scene.world_transform(*child_ptr->c_item()).transform_point({10.0f, 10.0f});
    const auto picked = hit_test(scene, inside_world);
    assert(picked && same(*picked, *child)); // deepest target beats its parent

    const auto clipped_world = scene.world_transform(*child_ptr->c_item()).transform_point({18.0f, 10.0f});
    const auto clipped_pick = hit_test(scene, clipped_world);
    assert(!clipped_pick || !same(*clipped_pick, *child));

    // Equal-z unrelated overlap follows stable scene order.
    const auto back = scene.adopt(
        std::make_unique<RectItem2D>(termin::Rect2f{100.0f, 0.0f, 20.0f, 20.0f}, tgfx::FillPaint{}, std::nullopt));
    const auto front =
        scene.adopt(std::make_unique<PathItem2D>(box(100.0f, 0.0f, 20.0f, 20.0f), tgfx::FillPaint{}, std::nullopt));
    assert(back && front);
    scene.resolve(*back)->z_order = 9;
    scene.resolve(*front)->z_order = 9;
    const auto overlap = hit_test(scene, {110.0f, 10.0f});
    assert(overlap && same(*overlap, *front));

    // Singular transforms are diagnosed during scene preparation and are
    // deterministically non-hittable without an identity fallback.
    scene.resolve(*front)->z_order = 20;
    scene.resolve(*front)->local_transform = termin::Affine2f::scaling(0.0f, 1.0f);
    assert(scene.diagnostics(*scene.resolve(*front)) == GraphicItemDiagnostic2D::SingularWorldTransform);
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
    interaction.set_fallback_handler([&](const PointerEvent2D&) { ++fallbacks; });

    auto down = interaction.route(scene, {1, PointerEventKind2D::Down, inside_world, 0});
    assert(same(down.target, *child));
    assert(same(down.captured, *child));
    selection.handle(scene, down);
    assert(selection.selection().size() == 1);
    assert(drag.handle(scene, down));

    // A second pointer owns independent hover/press/capture state.
    auto second_down = interaction.route(scene, {2, PointerEventKind2D::Down, {105.0f, 5.0f}, 0});
    assert(same(second_down.target, *back));
    assert(same(interaction.captured(1), *child));
    assert(same(interaction.captured(2), *back));
    const auto second_outside = interaction.route(scene, {2, PointerEventKind2D::Move, {-500.0f, -500.0f}, 0});
    assert(same(second_outside.target, *back));

    const termin::Vec2f delta{8.0f, -3.0f};
    auto move = interaction.route(scene,
                                  {
                                      1,
                                      PointerEventKind2D::Move,
                                      {inside_world.x + delta.x, inside_world.y + delta.y},
                                      0,
                                  });
    assert(same(move.target, *child));
    assert(drag.handle(scene, move));
    const auto moved_world = scene.world_transform(*child_ptr->c_item()).transform_point({10.0f, 10.0f});
    assert(near(moved_world.x, inside_world.x + delta.x));
    assert(near(moved_world.y, inside_world.y + delta.y));

    // Release inside the moved target delivers one semantic action.
    auto up = interaction.route(scene, {1, PointerEventKind2D::Up, moved_world, 0});
    assert(same(up.target, *child));
    assert(up.action);
    assert(actions == 1);
    assert(tc_graphic_item_handle_is_invalid(interaction.captured(1)));
    drag.handle(scene, up);

    // Detaching only makes an item a root; it does not invalidate identity or
    // capture. Disabling and destruction do invalidate capture.
    assert(interaction.capture(scene, 3, *child));
    assert(child_ptr->detach());
    auto after_detach = interaction.route(scene, {3, PointerEventKind2D::Move, moved_world, 0});
    assert(same(after_detach.captured, *child));
    interaction.release(3);
    selection.reconcile(scene);
    assert(selection.selection().size() == 1);
    selection.clear();

    assert(interaction.capture(scene, 4, *back));
    scene.resolve(*back)->enabled = false;
    interaction.route(scene, {4, PointerEventKind2D::Move, {105.0f, 5.0f}, 0});
    assert(tc_graphic_item_handle_is_invalid(interaction.captured(4)));

    scene.resolve(*back)->enabled = true;
    assert(interaction.capture(scene, 5, *back));
    assert(scene.destroy(*back));
    interaction.route(scene, {5, PointerEventKind2D::Move, {105.0f, 5.0f}, 0});
    assert(tc_graphic_item_handle_is_invalid(interaction.captured(5)));

    // Unclaimed space reaches the plot/host fallback path.
    auto fallback = interaction.route(scene, {7, PointerEventKind2D::Down, {-1000.0f, -1000.0f}, 0});
    assert(fallback.used_fallback);
    assert(fallbacks >= 1);

    const auto detached_world = scene.world_transform(*child_ptr->c_item()).transform_point({10.0f, 10.0f});
    selection.handle(scene, interaction.route(scene, {8, PointerEventKind2D::Down, detached_world, 0}));
    assert(selection.selection().size() == 1);
    assert(scene.destroy(*child));
    selection.reconcile(scene);
    assert(selection.selection().empty());
    tc_visual_scene_destroy(scene_handle);
}
