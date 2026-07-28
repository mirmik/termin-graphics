#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include <cmath>
#include <variant>

#include "termin_visual_scene/scene2d.hpp"

namespace {

using namespace termin::visual;

tgfx::Path2f triangle() {
    tgfx::Path2f path;
    assert(path.move_to({0.0f, 0.0f}));
    assert(path.line_to({4.0f, 0.0f}));
    assert(path.line_to({2.0f, 3.0f}));
    assert(path.close());
    return path;
}

bool near(float a, float b) {
    return std::abs(a - b) < 1e-4f;
}

void assert_bounds(
    const std::optional<termin::Bounds2f>& value,
    float x0,
    float y0,
    float x1,
    float y1) {
    assert(value);
    assert(near(value->x0, x0));
    assert(near(value->y0, y0));
    assert(near(value->x1, x1));
    assert(near(value->y1, y1));
}

}  // namespace

int main() {
    VisualScene2D scene;

    const auto root = scene.create(GroupItem2D{});
    assert(root);
    GraphicItemState2D root_state;
    root_state.local_transform =
        termin::Affine2f::translation(10.0f, 20.0f) *
        termin::Affine2f::shear(0.5f, 0.0f);
    root_state.opacity = 0.5f;
    root_state.z_order = 4;
    root_state.clip = GeometricClip2D{triangle(), tgfx::FillRule::EvenOdd};
    assert(scene.set_state(*root, root_state));

    RectItem2D rect{
        {0.0f, 0.0f, 8.0f, 6.0f},
        {{1.0f, 0.0f, 0.0f, 1.0f}, tgfx::FillRule::NonZero},
        std::nullopt,
    };
    const auto child = scene.create(rect, *root);
    assert(child);
    GraphicItemState2D child_state;
    child_state.local_transform =
        termin::Affine2f::rotation(0.5f) *
        termin::Affine2f::scaling(2.0f, 1.5f);
    child_state.opacity = 0.25f;
    child_state.z_order = 4;
    child_state.clip = GeometricClip2D{triangle(), tgfx::FillRule::NonZero};
    assert(scene.set_state(*child, child_state));

    const auto child_snapshot = scene.snapshot(*child);
    assert(child_snapshot);
    const auto expected_world =
        root_state.local_transform * child_state.local_transform;
    assert(near(child_snapshot->world_transform.m00, expected_world.m00));
    assert(near(child_snapshot->world_transform.m01, expected_world.m01));
    assert(near(child_snapshot->world_transform.m10, expected_world.m10));
    assert(near(child_snapshot->world_transform.m11, expected_world.m11));
    assert(near(child_snapshot->world_transform.tx, expected_world.tx));
    assert(near(child_snapshot->world_transform.ty, expected_world.ty));
    assert(near(child_snapshot->effective_opacity, 0.125f));
    assert(child_snapshot->effective_visible);
    assert(child_snapshot->effective_clips.size() == 2);
    assert(child_snapshot->effective_clips[0].rule == tgfx::FillRule::EvenOdd);
    assert_bounds(child_snapshot->local_bounds, 0.0f, 0.0f, 8.0f, 6.0f);
    assert(scene.snapshot(*root)->local_bounds);

    const auto ellipse = scene.create(EllipseItem2D{
        {1.0f, 2.0f, 5.0f, 7.0f},
        {},
        tgfx::StrokePaint{{}, 2.0f},
    });
    const auto rounded = scene.create(RoundedRectItem2D{
        {1.0f, 2.0f, 3.0f, 4.0f}, 1.0f, {}, std::nullopt});
    const auto path = scene.create(
        PathItem2D{triangle(), tgfx::FillPaint{}, std::nullopt});
    const auto polyline = scene.create(PolylineItem2D{
        {{-1.0f, 1.0f}, {3.0f, 5.0f}}, tgfx::StrokePaint{}, false});
    const auto text = scene.create(TextItem2D{
        "hello",
        {"asset://fonts/ui"},
        {2.0f, 3.0f},
        16.0f,
        {},
        tgfx::TextAnchor2D::Center,
        {2.0f, 3.0f, 42.0f, 19.0f},
    });
    const auto image = scene.create(ImageItem2D{
        {"asset://images/icon"},
        {2.0f, 4.0f, 8.0f, 6.0f},
    });
    const auto hit = scene.create(HitRegionItem2D{
        triangle(), tgfx::FillRule::NonZero});
    const auto custom = scene.create(CustomBatchItem2D{
        "plot-series:main", {-5.0f, -6.0f, 7.0f, 8.0f}});
    assert(ellipse && rounded && path && polyline && text && image && hit && custom);

    const auto all = scene.snapshots();
    assert(all.size() == 10);
    for (std::size_t i = 1; i < all.size(); ++i) {
        if (all[i - 1].state.z_order == all[i].state.z_order) {
            assert(all[i - 1].stable_order < all[i].stable_order);
        } else {
            assert(all[i - 1].state.z_order < all[i].state.z_order);
        }
    }
    assert(std::holds_alternative<TextItem2D>(scene.snapshot(*text)->payload));
    assert(std::get<TextItem2D>(scene.snapshot(*text)->payload).font.uri ==
           "asset://fonts/ui");
    assert(std::get<ImageItem2D>(scene.snapshot(*image)->payload).image.uri ==
           "asset://images/icon");
    assert(tc_runtime_type_registry_instance_count(
               "termin.visual.Group2D") == 1);
    assert(tc_runtime_type_registry_instance_count(
               "termin.visual.Rect2D") == 1);
    assert(tc_runtime_type_registry_instance_count(
               "termin.visual.RoundedRect2D") == 1);

    // A payload type change swaps the concrete embedded-base object without
    // changing the public handle or retaining the previous typed body.
    const auto child_handle = *child;
    assert(scene.set_payload(
        *child,
        RoundedRectItem2D{
            rect.rect, 1.5f, rect.fill, rect.stroke}));
    assert(scene.contains(child_handle));
    assert(std::holds_alternative<RoundedRectItem2D>(
        scene.snapshot(child_handle)->payload));
    assert(tc_runtime_type_registry_instance_count(
               "termin.visual.Rect2D") == 0);
    assert(tc_runtime_type_registry_instance_count(
               "termin.visual.RoundedRect2D") == 2);
    assert(scene.set_payload(*child, rect));

    // Invalid updates are transactional and do not advance either revision.
    const auto before = scene.snapshot(*child);
    const auto scene_revision = scene.revision();
    auto invalid_state = before->state;
    invalid_state.opacity = 2.0f;
    assert(!scene.set_state(*child, invalid_state));
    assert(scene.revision() == scene_revision);
    assert(scene.snapshot(*child)->revision == before->revision);

    PathItem2D invalid_path;
    invalid_path.fill = tgfx::FillPaint{};
    assert(!scene.set_payload(*child, invalid_path));
    assert(scene.revision() == scene_revision);
    assert(std::holds_alternative<RectItem2D>(scene.snapshot(*child)->payload));

    auto singular = before->state;
    singular.local_transform = termin::Affine2f::scaling(0.0f, 1.0f);
    assert(scene.set_state(*child, singular));
    assert(scene.snapshot(*child)->diagnostics ==
           GraphicItemDiagnostic2D::SingularWorldTransform);

    assert(scene.destroy_subtree(*root));
    assert(!scene.snapshot(*child));
    assert(scene.size() == 8);
}
