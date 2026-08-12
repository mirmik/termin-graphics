#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

#include <termin/geom/quat.hpp>
#include <termin/geom/vec3.hpp>

#include "termin_visual_scene/scene3d.hpp"
#include "termin_visual_scene/visual_item3d.hpp"

namespace {

    class TestItem final : public termin::visual::VisualItem3D {
    public:
        TestItem(int value, int& deletes, int& destroys)
            : VisualItem3D(&VTABLE, "termin.visual.test.TestItem3D"),
              value(value),
              deletes_(deletes),
              destroys_(destroys) {}

        ~TestItem() override {
            ++deletes_;
        }

        int value = 0;

    private:
        static void on_destroy(tc_visual_item3d* item, tc_visual_scene3d* scene) {
            auto* self = static_cast<TestItem*>(item->body);
            assert(self != nullptr);
            assert(scene != nullptr);
            assert(item->scene == nullptr);
            assert(tc_visual_item3d_handle_is_invalid(item->handle));
            ++self->destroys_;
        }

        static const tc_visual_item3d_vtable VTABLE;
        int& deletes_;
        int& destroys_;
    };

    const tc_visual_item3d_vtable TestItem::VTABLE{
        .type_name = "termin.visual.test.TestItem3D",
        .on_destroy = TestItem::on_destroy,
    };

    bool close(double left, double right) {
        return std::abs(left - right) < 1.0e-10;
    }

} // namespace

int main() {
    using termin::Affine3d;
    using termin::Quat;
    using termin::Vec3;
    using termin::visual::TcVisualScene3D;

    int deletes = 0;
    int destroys = 0;
    const auto scene_handle = tc_visual_scene3d_create();
    assert(tc_visual_scene3d_is_valid(scene_handle));
    TcVisualScene3D scene{scene_handle};

    auto root = std::make_unique<TestItem>(1, deletes, destroys);
    auto* root_ptr = root.get();
    const auto root_handle = scene.adopt(std::move(root));
    assert(root_handle);

    auto child = std::make_unique<TestItem>(2, deletes, destroys);
    auto* child_ptr = child.get();
    const auto child_handle = scene.adopt(std::move(child), root_ptr);
    assert(child_handle);
    assert(scene.size() == 2);
    assert(child_ptr->parent_item() == root_ptr->c_item());
    const auto initial_items = scene.items();
    assert(initial_items.size() == 2);
    assert(initial_items[0] == root_ptr->c_item());
    assert(initial_items[1] == child_ptr->c_item());

    const auto parent_transform =
        Affine3d::from_rotation(Quat::from_axis_angle(Vec3::unit_z(), 0.5)) * Affine3d::scaling(2.0, 3.0, 4.0);
    const auto child_transform = Affine3d::from_translation(1.0, -2.0, 5.0) *
                                 Affine3d::from_rotation(Quat::from_axis_angle(Vec3::unit_x(), 0.3));
    root_ptr->set_local_transform(parent_transform);
    child_ptr->set_local_transform(child_transform);

    const Affine3d expected = parent_transform * child_transform;
    const Affine3d actual = scene.world_transform(*child_ptr->c_item());
    const Vec3 probe{0.25, -0.75, 1.5};
    const Vec3 expected_point = expected.transform_point(probe);
    const Vec3 actual_point = actual.transform_point(probe);
    assert(close(expected_point.x, actual_point.x));
    assert(close(expected_point.y, actual_point.y));
    assert(close(expected_point.z, actual_point.z));

    root_ptr->set_visible(false);
    assert(!scene.effective_visible(*child_ptr->c_item()));
    root_ptr->set_visible(true);
    root_ptr->set_enabled(false);
    assert(!scene.effective_enabled(*child_ptr->c_item()));
    root_ptr->set_enabled(true);

    bool rejected_non_finite = false;
    try {
        auto invalid = Affine3d::identity();
        invalid.translation.x = std::numeric_limits<double>::quiet_NaN();
        child_ptr->set_local_transform(invalid);
    } catch (const std::invalid_argument&) {
        rejected_non_finite = true;
    }
    assert(rejected_non_finite);

    auto replacement = std::make_unique<TestItem>(20, deletes, destroys);
    auto* replacement_ptr = replacement.get();
    assert(scene.replace(*child_handle, std::move(replacement)));
    assert(deletes == 1);
    assert(destroys == 1);
    assert(scene.resolve(*child_handle) == replacement_ptr->c_item());
    assert(replacement_ptr->parent_item() == root_ptr->c_item());
    assert(close(replacement_ptr->local_transform().translation.x, child_transform.translation.x));
    const auto replaced_items = scene.items();
    assert(replaced_items[0] == root_ptr->c_item());
    assert(replaced_items[1] == replacement_ptr->c_item());

    const auto foreign_scene_handle = tc_visual_scene3d_create();
    TcVisualScene3D foreign_scene{foreign_scene_handle};
    auto foreign = std::make_unique<TestItem>(3, deletes, destroys);
    auto* foreign_ptr = foreign.get();
    const auto foreign_item_handle = foreign_scene.adopt(std::move(foreign));
    assert(foreign_item_handle);
    assert(!root_ptr->append_child(*foreign_ptr));
    assert(!scene.contains(*foreign_item_handle));

    assert(scene.destroy(*child_handle));
    assert(deletes == 2);
    assert(destroys == 2);
    assert(scene.resolve(*child_handle) == nullptr);

    auto reused = std::make_unique<TestItem>(4, deletes, destroys);
    const auto reused_handle = scene.adopt(std::move(reused));
    assert(reused_handle);
    if (reused_handle->index == child_handle->index) {
        assert(reused_handle->generation != child_handle->generation);
    }

    scene.clear();
    foreign_scene.clear();
    assert(deletes == 5);
    assert(destroys == 5);
    tc_visual_scene3d_destroy(foreign_scene_handle);
    tc_visual_scene3d_destroy(scene_handle);
}
