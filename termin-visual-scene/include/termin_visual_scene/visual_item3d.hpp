#pragma once

#include <cstddef>

#include "termin_visual_scene/scene3d.hpp"

namespace termin::visual {

    class TERMIN_VISUAL_SCENE_API VisualItem3D {
    public:
        virtual ~VisualItem3D();

        VisualItem3D(const VisualItem3D&) = delete;
        VisualItem3D& operator=(const VisualItem3D&) = delete;

        tc_visual_item3d* c_item() noexcept {
            return &item_;
        }
        const tc_visual_item3d* c_item() const noexcept {
            return &item_;
        }
        VisualItem3DHandle handle() const noexcept {
            return item_.handle;
        }

        tc_visual_item3d* parent_item() noexcept {
            return tc_visual_item3d_parent(&item_);
        }
        const tc_visual_item3d* parent_item() const noexcept {
            return tc_visual_item3d_parent_const(&item_);
        }
        std::size_t child_count() const noexcept {
            return item_.child_count;
        }
        tc_visual_item3d* child_at(std::size_t index) noexcept {
            return tc_visual_item3d_child_at(&item_, index);
        }
        const tc_visual_item3d* child_at(std::size_t index) const noexcept {
            return tc_visual_item3d_child_at_const(&item_, index);
        }
        bool append_child(VisualItem3D& child);
        bool insert_child(std::size_t index, VisualItem3D& child);
        bool remove_child(VisualItem3D& child);
        bool detach();

        const termin::Affine3d& local_transform() const noexcept {
            return item_.local_transform;
        }
        void set_local_transform(termin::Affine3d transform);
        bool visible() const noexcept {
            return item_.visible;
        }
        void set_visible(bool visible) noexcept {
            item_.visible = visible;
        }
        bool enabled() const noexcept {
            return item_.enabled;
        }
        void set_enabled(bool enabled) noexcept {
            item_.enabled = enabled;
        }

        static void delete_owned_item(tc_visual_item3d* item) {
            delete static_cast<VisualItem3D*>(item->body);
        }

    protected:
        VisualItem3D(const tc_visual_item3d_vtable* vtable, const char* declared_type_name);

    private:
        tc_visual_item3d item_{};
    };

} // namespace termin::visual
