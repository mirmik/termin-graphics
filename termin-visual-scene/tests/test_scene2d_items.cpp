#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <memory>
#include <string>

#include <termin/geom/color.hpp>

#include "termin_visual_scene/builtin_items2d.hpp"

namespace {

    using namespace termin::visual;

    bool near(float a, float b) {
        return std::abs(a - b) < 1e-4f;
    }

    void assert_bounds(const std::optional<termin::Bounds2f>& value, float x0, float y0, float x1, float y1) {
        assert(value);
        assert(near(value->x0, x0));
        assert(near(value->y0, y0));
        assert(near(value->x1, x1));
        assert(near(value->y1, y1));
    }

    tgfx::Path2f triangle() {
        tgfx::Path2f path;
        assert(path.move_to({0.0f, 0.0f}));
        assert(path.line_to({4.0f, 0.0f}));
        assert(path.line_to({2.0f, 3.0f}));
        assert(path.close());
        return path;
    }

} // namespace

int main() {
    const auto scene_handle = tc_visual_scene_create();
    TcVisualScene scene{scene_handle};

    auto root_object = std::make_unique<GroupItem2D>();
    GroupItem2D* root_ptr = root_object.get();
    const auto root = scene.adopt(std::move(root_object));
    assert(root);
    const auto root_transform = termin::Affine2f::translation(10.0f, 20.0f) * termin::Affine2f::shear(0.5f, 0.0f);
    root_ptr->set_local_transform(root_transform);
    root_ptr->set_opacity(0.5f);
    root_ptr->set_z_order(4);
    root_ptr->set_clip(GeometricClip2D{triangle(), tgfx::FillRule::EvenOdd});

    auto child_object =
        std::make_unique<RectItem2D>(termin::Rect2f{0.0f, 0.0f, 8.0f, 6.0f}, tgfx::FillPaint{}, std::nullopt);
    RectItem2D* child_ptr = child_object.get();
    const auto child = scene.adopt(std::move(child_object), root_ptr);
    assert(child);
    const auto child_transform = termin::Affine2f::rotation(0.5f) * termin::Affine2f::scaling(2.0f, 1.5f);
    child_ptr->set_local_transform(child_transform);
    child_ptr->set_opacity(0.25f);
    child_ptr->set_z_order(4);
    child_ptr->set_clip(GeometricClip2D{triangle(), tgfx::FillRule::NonZero});

    const auto expected_world = root_transform * child_transform;
    const auto world = scene.world_transform(*child_ptr->c_item());
    assert(near(world.m00, expected_world.m00));
    assert(near(world.m01, expected_world.m01));
    assert(near(world.m10, expected_world.m10));
    assert(near(world.m11, expected_world.m11));
    assert(near(world.tx, expected_world.tx));
    assert(near(world.ty, expected_world.ty));
    assert(near(scene.effective_opacity(*child_ptr->c_item()), 0.125f));
    assert(scene.effective_visible(*child_ptr->c_item()));
    assert_bounds(scene.local_bounds(*child_ptr->c_item()), 0.0f, 0.0f, 8.0f, 6.0f);
    const auto expected_root_bounds = child_transform.transform_bounds({0.0f, 0.0f, 8.0f, 6.0f});
    assert_bounds(scene.local_bounds(*root_ptr->c_item()),
                  expected_root_bounds.x0,
                  expected_root_bounds.y0,
                  expected_root_bounds.x1,
                  expected_root_bounds.y1);
    // World projection is evaluated from the source geometry through the
    // accumulated affine in one step, not by re-projecting the local AABB.
    const auto expected_world_bounds = expected_world.transform_bounds({0.0f, 0.0f, 8.0f, 6.0f});
    assert_bounds(scene.world_bounds(*root_ptr->c_item()),
                  expected_world_bounds.x0,
                  expected_world_bounds.y0,
                  expected_world_bounds.x1,
                  expected_world_bounds.y1);

    const auto ellipse = scene.adopt(std::make_unique<EllipseItem2D>(
        termin::Rect2f{1.0f, 2.0f, 5.0f, 7.0f}, tgfx::FillPaint{}, tgfx::StrokePaint{{}, 2.0f}));
    const auto rounded = scene.adopt(std::make_unique<RoundedRectItem2D>(
        termin::Rect2f{1.0f, 2.0f, 3.0f, 4.0f}, 1.0f, tgfx::FillPaint{}, std::nullopt));
    const auto path = scene.adopt(std::make_unique<PathItem2D>(triangle(), tgfx::FillPaint{}, std::nullopt));
    const auto polyline = scene.adopt(std::make_unique<PolylineItem2D>(
        std::vector<termin::Vec2f>{{-1.0f, 1.0f}, {3.0f, 5.0f}}, tgfx::StrokePaint{}, false));
    const auto text = scene.adopt(std::make_unique<TextItem2D>("hello",
                                                               "asset://fonts/ui",
                                                               termin::Vec2f{2.0f, 3.0f},
                                                               16.0f,
                                                               termin::SrgbColor{},
                                                               tgfx::TextAnchor2D::Center,
                                                               termin::Bounds2f{2.0f, 3.0f, 42.0f, 19.0f}));
    const auto image = scene.adopt(std::make_unique<ImageItem2D>("asset://images/icon",
                                                                 termin::Rect2f{2.0f, 4.0f, 8.0f, 6.0f},
                                                                 termin::Rect2f{0.0f, 0.0f, 1.0f, 1.0f},
                                                                 termin::SrgbColor{},
                                                                 tgfx::DrawTextureSampling2D::Linear));
    const auto hit = scene.adopt(std::make_unique<HitRegionItem2D>(triangle(), tgfx::FillRule::NonZero));
    const auto custom = scene.adopt(
        std::make_unique<CustomBatchItem2D>("plot-series:main", termin::Bounds2f{-5.0f, -6.0f, 7.0f, 8.0f}));
    assert(ellipse && rounded && path && polyline && text && image && hit && custom);
    assert(scene.size() == 10);

    const auto child_handle = *child;
    assert(scene.replace(child_handle,
                         std::make_unique<RoundedRectItem2D>(
                             termin::Rect2f{0.0f, 0.0f, 8.0f, 6.0f}, 1.5f, tgfx::FillPaint{}, std::nullopt)));
    tc_graphic_item* replaced = scene.resolve(child_handle);
    assert(replaced);
    assert(std::string(tc_graphic_item_type_name(replaced)) == "termin.visual.RoundedRect2D");
    assert(replaced->parent == root_ptr->c_item());

    replaced->local_transform = termin::Affine2f::scaling(0.0f, 1.0f);
    assert(scene.diagnostics(*replaced) == GraphicItemDiagnostic2D::SingularWorldTransform);
    assert(scene.world_bounds(*replaced));

    assert(scene.destroy(*root));
    assert(!scene.contains(child_handle));
    assert(scene.size() == 8);
    tc_visual_scene_destroy(scene_handle);
}
