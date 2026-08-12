#include "termin_visual_scene/scene3d.hpp"

#include <tcbase/tc_log.hpp>

#include "termin_visual_scene/visual_item3d.hpp"

namespace termin::visual {

    std::optional<VisualItem3DHandle>
    TcVisualScene3D::adopt(tc_visual_item3d* item, tc_visual_item3d_deleter deleter, tc_visual_item3d* parent) {
        if (parent != nullptr && !owns_(*parent)) {
            tc::Log::error("TcVisualScene3D::adopt rejected foreign parent");
            if (item != nullptr && deleter != nullptr) {
                deleter(item);
            }
            return std::nullopt;
        }
        const auto adopted = tc_visual_scene3d_adopt_item(handle_, item, deleter);
        if (tc_visual_item3d_handle_is_invalid(adopted)) {
            return std::nullopt;
        }
        if (parent != nullptr && !tc_visual_item3d_append_child(parent, item)) {
            tc_visual_scene3d_destroy_item(handle_, adopted);
            return std::nullopt;
        }
        return adopted;
    }

    std::optional<VisualItem3DHandle> TcVisualScene3D::adopt(std::unique_ptr<VisualItem3D> item, VisualItem3D* parent) {
        if (!item) {
            tc::Log::error("TcVisualScene3D::adopt received null item");
            return std::nullopt;
        }
        auto* owned = item.release();
        return adopt(owned->c_item(), &VisualItem3D::delete_owned_item, parent != nullptr ? parent->c_item() : nullptr);
    }

    bool TcVisualScene3D::replace(VisualItem3DHandle handle,
                                  tc_visual_item3d* replacement,
                                  tc_visual_item3d_deleter deleter) {
        return tc_visual_scene3d_replace_item(handle_, handle, replacement, deleter);
    }

    bool TcVisualScene3D::replace(VisualItem3DHandle handle, std::unique_ptr<VisualItem3D> replacement) {
        if (!replacement)
            return false;
        auto* owned = replacement.release();
        return replace(handle, owned->c_item(), &VisualItem3D::delete_owned_item);
    }

    bool TcVisualScene3D::destroy(VisualItem3DHandle handle) {
        return tc_visual_scene3d_destroy_item(handle_, handle);
    }

    void TcVisualScene3D::clear() {
        tc_visual_scene3d_clear(handle_);
    }

    tc_visual_item3d* TcVisualScene3D::resolve(VisualItem3DHandle handle) {
        return tc_visual_scene3d_resolve_item(handle_, handle);
    }

    const tc_visual_item3d* TcVisualScene3D::resolve(VisualItem3DHandle handle) const {
        return tc_visual_scene3d_resolve_item_const(handle_, handle);
    }

    std::vector<tc_visual_item3d*> TcVisualScene3D::items() {
        std::vector<tc_visual_item3d*> result(tc_visual_scene3d_copy_items(handle_, nullptr, 0));
        for (;;) {
            const auto count = tc_visual_scene3d_copy_items(handle_, result.data(), result.size());
            if (count <= result.size()) {
                result.resize(count);
                return result;
            }
            result.resize(count);
        }
    }

    bool TcVisualScene3D::owns_(const tc_visual_item3d& item) const {
        return item.handle.scene_id == id() && resolve(item.handle) == &item;
    }

    termin::Affine3d TcVisualScene3D::world_transform(const tc_visual_item3d& item) const {
        if (!owns_(item)) {
            tc::Log::error("TcVisualScene3D::world_transform rejected a foreign item");
            return termin::Affine3d::identity();
        }
        termin::Affine3d result = termin::Affine3d::identity();
        std::vector<const tc_visual_item3d*> ancestry;
        for (const tc_visual_item3d* cursor = &item; cursor != nullptr; cursor = cursor->parent) {
            ancestry.push_back(cursor);
        }
        for (auto cursor = ancestry.rbegin(); cursor != ancestry.rend(); ++cursor) {
            result = result * (*cursor)->local_transform;
        }
        return result;
    }

    bool TcVisualScene3D::effective_visible(const tc_visual_item3d& item) const {
        if (!owns_(item))
            return false;
        for (const tc_visual_item3d* cursor = &item; cursor != nullptr; cursor = cursor->parent) {
            if (!cursor->visible)
                return false;
        }
        return true;
    }

    bool TcVisualScene3D::effective_enabled(const tc_visual_item3d& item) const {
        if (!owns_(item))
            return false;
        for (const tc_visual_item3d* cursor = &item; cursor != nullptr; cursor = cursor->parent) {
            if (!cursor->enabled)
                return false;
        }
        return true;
    }

} // namespace termin::visual
