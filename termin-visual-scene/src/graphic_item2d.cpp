#include "termin_visual_scene/graphic_item2d.hpp"

#include <cmath>
#include <stdexcept>

#include "tc_visual_scene_internal.h"

namespace termin::visual {

GraphicItem2D::GraphicItem2D(
    const tc_graphic_item_vtable* vtable,
    const char* declared_type_name)
{
    tc_graphic_item_init_unowned(
        &item_, vtable, TC_LANGUAGE_CXX, this);
    tc_graphic_item_set_declared_type_name(
        &item_, declared_type_name);
}

GraphicItem2D::~GraphicItem2D() {
    tc_runtime_type_registry_unlink_instance(
        &item_.runtime_type_link);
}

std::size_t GraphicItem2D::child_count() const noexcept {
    return item_.child_count;
}

bool GraphicItem2D::append_child(
    GraphicItem2D& child)
{
    return tc_graphic_item_append_child(
        &item_, child.c_item());
}

bool GraphicItem2D::insert_child(
    std::size_t index,
    GraphicItem2D& child)
{
    return tc_graphic_item_insert_child(
        &item_, index, child.c_item());
}

bool GraphicItem2D::remove_child(
    GraphicItem2D& child)
{
    return tc_graphic_item_remove_child(
        &item_, child.c_item());
}

bool GraphicItem2D::detach() {
    return tc_graphic_item_detach(&item_);
}

void GraphicItem2D::set_local_transform(
    termin::Affine2f transform)
{
    if (!transform.is_finite()) {
        throw std::invalid_argument(
            "graphic item transform must be finite");
    }
    item_.local_transform = transform;
}

void GraphicItem2D::set_opacity(float opacity) {
    if (!std::isfinite(opacity) ||
        opacity < 0.0f || opacity > 1.0f) {
        throw std::invalid_argument(
            "graphic item opacity must be in [0, 1]");
    }
    item_.opacity = opacity;
}

void GraphicItem2D::set_z_order(std::int64_t z_order) noexcept {
    if (item_.z_order == z_order) return;
    item_.z_order = z_order;
    tc_visual_scene_touch_order(item_.scene);
}

void GraphicItem2D::set_clip(
    std::optional<GeometricClip2D> clip)
{
    if (clip &&
        (clip->path.empty() ||
         (clip->rule != tgfx::FillRule::NonZero &&
          clip->rule != tgfx::FillRule::EvenOdd))) {
        throw std::invalid_argument(
            "invalid graphic item clip");
    }
    clip_ = std::move(clip);
}

}  // namespace termin::visual
