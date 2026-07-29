#pragma once

#include <cstddef>
#include <optional>

#include "termin_visual_scene/scene2d.hpp"

namespace termin::visual {

class TERMIN_VISUAL_SCENE_API GraphicItem2D {
public:
    virtual ~GraphicItem2D();

    GraphicItem2D(const GraphicItem2D&) = delete;
    GraphicItem2D& operator=(const GraphicItem2D&) = delete;

    tc_graphic_item* c_item() noexcept { return &item_; }
    const tc_graphic_item* c_item() const noexcept {
        return &item_;
    }
    GraphicItemHandle handle() const noexcept {
        return item_.handle;
    }

    tc_graphic_item* parent_item() noexcept {
        return tc_graphic_item_parent(&item_);
    }
    const tc_graphic_item* parent_item() const noexcept {
        return tc_graphic_item_parent_const(&item_);
    }
    std::size_t child_count() const noexcept;
    tc_graphic_item* child_at(std::size_t index) noexcept {
        return tc_graphic_item_child_at(&item_, index);
    }
    const tc_graphic_item* child_at(
        std::size_t index) const noexcept {
        return tc_graphic_item_child_at_const(
            &item_, index);
    }
    bool append_child(GraphicItem2D& child);
    bool insert_child(
        std::size_t index,
        GraphicItem2D& child);
    bool remove_child(GraphicItem2D& child);
    bool detach();

    const termin::Affine2f& local_transform() const noexcept {
        return item_.local_transform;
    }
    void set_local_transform(termin::Affine2f transform);
    bool visible() const noexcept { return item_.visible; }
    void set_visible(bool visible) noexcept {
        item_.visible = visible;
    }
    bool enabled() const noexcept { return item_.enabled; }
    void set_enabled(bool enabled) noexcept {
        item_.enabled = enabled;
    }
    float opacity() const noexcept { return item_.opacity; }
    void set_opacity(float opacity);
    std::int64_t z_order() const noexcept {
        return item_.z_order;
    }
    void set_z_order(std::int64_t z_order) noexcept {
        item_.z_order = z_order;
    }
    void set_clip(std::optional<GeometricClip2D> clip);
    const std::optional<GeometricClip2D>& clip() const noexcept {
        return clip_;
    }

    static void delete_owned_item(tc_graphic_item* item) {
        delete static_cast<GraphicItem2D*>(item->body);
    }

protected:
    GraphicItem2D(
        const tc_graphic_item_vtable* vtable,
        const char* declared_type_name);

private:
    tc_graphic_item item_{};
    std::optional<GeometricClip2D> clip_;
};

}  // namespace termin::visual
