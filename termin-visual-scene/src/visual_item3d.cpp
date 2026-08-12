#include "termin_visual_scene/visual_item3d.hpp"

#include <stdexcept>

namespace termin::visual {

    VisualItem3D::VisualItem3D(const tc_visual_item3d_vtable* vtable, const char* declared_type_name) {
        tc_visual_item3d_init_unowned(&item_, vtable, TC_LANGUAGE_CXX, this);
        tc_visual_item3d_set_declared_type_name(&item_, declared_type_name);
    }

    VisualItem3D::~VisualItem3D() {
        tc_runtime_type_registry_unlink_instance(&item_.runtime_type_link);
    }

    bool VisualItem3D::append_child(VisualItem3D& child) {
        return tc_visual_item3d_append_child(&item_, child.c_item());
    }

    bool VisualItem3D::insert_child(std::size_t index, VisualItem3D& child) {
        return tc_visual_item3d_insert_child(&item_, index, child.c_item());
    }

    bool VisualItem3D::remove_child(VisualItem3D& child) {
        return tc_visual_item3d_remove_child(&item_, child.c_item());
    }

    bool VisualItem3D::detach() {
        return tc_visual_item3d_detach(&item_);
    }

    void VisualItem3D::set_local_transform(termin::Affine3d transform) {
        if (!transform.is_finite()) {
            throw std::invalid_argument("visual item3d transform must be finite");
        }
        item_.local_transform = transform;
    }

} // namespace termin::visual
