#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

#include "termin_visual_scene/interaction3d.hpp"
#include "termin_visual_scene/visual_item3d.hpp"

namespace {

    bool close(double left, double right) {
        return std::abs(left - right) < 1.0e-10;
    }

    class HitItem final : public termin::visual::VisualItem3D {
    public:
        HitItem(double distance, std::uint64_t part)
            : VisualItem3D(&VTABLE, "termin.visual.test.HitItem3D"),
              distance(distance),
              part(part) {}

        double distance = 1.0;
        std::uint64_t part = 0;
        bool hittable = true;
        bool valid_bounds = true;
        mutable std::size_t hit_calls = 0;
        mutable tc_visual_hit_test_context3d last_context{};

    private:
        static bool hit_test(const tc_visual_item3d* item,
                             const tc_visual_hit_test_context3d* context,
                             tc_visual_hit_candidate3d* out) {
            const auto* self = static_cast<const HitItem*>(item->body);
            ++self->hit_calls;
            self->last_context = *context;
            if (!self->hittable)
                return false;
            out->distance = self->distance;
            out->part = self->part;
            return true;
        }

        static bool local_bounds(const tc_visual_item3d* item, tc_visual_bounds3d* out) {
            const auto* self = static_cast<const HitItem*>(item->body);
            if (self->valid_bounds) {
                *out = {{-1.0, -2.0, -3.0}, {1.0, 2.0, 3.0}};
            } else {
                *out = {{1.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}};
            }
            return true;
        }

        static const tc_visual_item3d_vtable VTABLE;
    };

    const tc_visual_item3d_vtable HitItem::VTABLE{
        .type_name = "termin.visual.test.HitItem3D",
        .hit_test = HitItem::hit_test,
        .local_bounds = HitItem::local_bounds,
    };

    termin::Ray3 ray(double y = 0.0) {
        return {{0.0, y, 0.0}, {2.0, 0.0, 0.0}};
    }

} // namespace

int main() {
    using termin::Affine3d;
    using termin::visual::PointerEvent3D;
    using termin::visual::PointerEventKind3D;
    using termin::visual::SceneInteraction3D;
    using termin::visual::TcVisualScene3D;

    const auto scene_handle = tc_visual_scene3d_create();
    TcVisualScene3D scene{scene_handle};

    auto far = std::make_unique<HitItem>(8.0, 80);
    auto* far_ptr = far.get();
    const auto far_handle = scene.adopt(std::move(far));
    assert(far_handle);

    auto near = std::make_unique<HitItem>(3.0, 30);
    auto* near_ptr = near.get();
    near_ptr->set_local_transform(Affine3d::from_translation(4.0, 0.0, 0.0) * Affine3d::scaling(2.0, 3.0, 4.0));
    const auto near_handle = scene.adopt(std::move(near));
    assert(near_handle);

    auto nearest = termin::visual::hit_test(scene, ray());
    assert(nearest);
    assert(tc_visual_item3d_handle_eq(nearest->item, *near_handle));
    assert(nearest->part == 30);
    assert(close(nearest->distance, 3.0));
    assert(close(nearest->world_point.x, 3.0));
    assert(close(near_ptr->last_context.world_ray.direction.x, 1.0));
    assert(close(near_ptr->last_context.local_ray.direction.x, 0.5));
    assert(close(near_ptr->last_context.local_ray.origin.x, -2.0));
    assert(close(nearest->local_point.x, -0.5));

    // Exact ties retain the earliest stable adoption order.
    far_ptr->distance = 3.0;
    nearest = termin::visual::hit_test(scene, ray());
    assert(nearest && tc_visual_item3d_handle_eq(nearest->item, *far_handle));
    far_ptr->distance = 8.0;

    tc_visual_bounds3d bounds{};
    assert(tc_visual_item3d_local_bounds_in_scene(scene_handle, *near_handle, &bounds));
    assert(bounds.min.y == -2.0 && bounds.max.z == 3.0);
    near_ptr->valid_bounds = false;
    assert(!tc_visual_item3d_local_bounds_in_scene(scene_handle, *near_handle, &bounds));
    near_ptr->valid_bounds = true;

    auto parent = std::make_unique<HitItem>(20.0, 0);
    auto* parent_ptr = parent.get();
    parent_ptr->hittable = false;
    const auto parent_handle = scene.adopt(std::move(parent));
    assert(parent_handle);
    assert(parent_ptr->append_child(*near_ptr));
    parent_ptr->set_visible(false);
    nearest = termin::visual::hit_test(scene, ray());
    assert(nearest && tc_visual_item3d_handle_eq(nearest->item, *far_handle));
    parent_ptr->set_visible(true);

    auto singular = std::make_unique<HitItem>(1.0, 10);
    auto* singular_ptr = singular.get();
    singular_ptr->set_local_transform(Affine3d::scaling(0.0, 1.0, 1.0));
    const auto singular_handle = scene.adopt(std::move(singular));
    assert(singular_handle);
    nearest = termin::visual::hit_test(scene, ray());
    assert(nearest && tc_visual_item3d_handle_eq(nearest->item, *near_handle));

    singular_ptr->set_enabled(false);
    near_ptr->distance = std::numeric_limits<double>::quiet_NaN();
    nearest = termin::visual::hit_test(scene, ray());
    assert(nearest && tc_visual_item3d_handle_eq(nearest->item, *far_handle));
    near_ptr->distance = 3.0;

    SceneInteraction3D interaction;
    int actions = 0;
    int fallbacks = 0;
    interaction.set_action_handler(*near_handle, [&](const auto& action) {
        ++actions;
        assert(action.part == 30);
    });
    interaction.set_fallback_handler([&](const auto&) { ++fallbacks; });

    auto down = interaction.route(scene, PointerEvent3D{7, PointerEventKind3D::Down, ray(), 1});
    assert(tc_visual_item3d_handle_eq(down.target, *near_handle));
    assert(tc_visual_item3d_handle_eq(down.pressed, *near_handle));
    assert(tc_visual_item3d_handle_eq(down.captured, *near_handle));

    near_ptr->hittable = false;
    auto move = interaction.route(scene, PointerEvent3D{7, PointerEventKind3D::Move, ray(), 1});
    assert(tc_visual_item3d_handle_eq(move.target, *near_handle));
    assert(!move.hit || !tc_visual_item3d_handle_eq(move.hit->item, *near_handle));
    near_ptr->hittable = true;

    auto up = interaction.route(scene, PointerEvent3D{7, PointerEventKind3D::Up, ray(), 1});
    assert(up.action);
    assert(actions == 1);
    assert(tc_visual_item3d_handle_is_invalid(up.captured));

    interaction.set_action_handler(*near_handle, [](const auto&) { throw std::runtime_error("expected action"); });
    interaction.route(scene, PointerEvent3D{12, PointerEventKind3D::Down, ray(), 1});
    up = interaction.route(scene, PointerEvent3D{12, PointerEventKind3D::Up, ray(), 1});
    assert(up.action && up.callback_failed);

    // A different item-defined part is not the same click target.
    interaction.route(scene, PointerEvent3D{8, PointerEventKind3D::Down, ray(), 1});
    near_ptr->part = 31;
    up = interaction.route(scene, PointerEvent3D{8, PointerEventKind3D::Up, ray(), 1});
    assert(!up.action);
    assert(actions == 1);
    near_ptr->part = 30;

    far_ptr->hittable = false;
    near_ptr->hittable = false;
    auto miss = interaction.route(scene, PointerEvent3D{9, PointerEventKind3D::Move, ray(50.0), 0});
    assert(miss.used_fallback);
    assert(fallbacks == 1);
    near_ptr->hittable = true;
    far_ptr->hittable = true;

    assert(near_ptr->detach());
    parent_ptr->set_enabled(false);
    assert(interaction.capture(scene, 10, *near_handle));
    assert(parent_ptr->append_child(*near_ptr));
    move = interaction.route(scene, PointerEvent3D{10, PointerEventKind3D::Move, ray(), 0});
    assert(tc_visual_item3d_handle_is_invalid(move.captured));
    assert(!tc_visual_item3d_handle_eq(move.target, *near_handle));
    parent_ptr->set_enabled(true);

    interaction.set_fallback_handler([](const auto&) { throw std::runtime_error("expected"); });
    near_ptr->hittable = false;
    far_ptr->hittable = false;
    miss = interaction.route(scene, PointerEvent3D{11, PointerEventKind3D::Move, ray(), 0});
    assert(miss.callback_failed);

    scene.clear();
    tc_visual_scene3d_destroy(scene_handle);
}
