#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <termin/geom/affine3.hpp>
#include <termin/geom/ray3.hpp>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/tc_visual_scene3d.h"

namespace termin::visual {

    class VisualItem3D;
    using VisualItem3DHandle = tc_visual_item3d_handle;
    using VisualBounds3D = tc_visual_bounds3d;
    using HitTestContext3D = tc_visual_hit_test_context3d;
    using HitCandidate3D = tc_visual_hit_candidate3d;
    using HitResult3D = tc_visual_hit_result3d;

    TERMIN_VISUAL_SCENE_API std::optional<HitResult3D> hit_test(const class TcVisualScene3D& scene,
                                                                termin::Ray3 world_ray);

    // Copyable, non-owning facade over the pooled C scene handle. Creation and
    // destruction remain explicit tc_visual_scene3d_create/destroy operations,
    // matching TcVisualScene's established lifetime contract.
    class TERMIN_VISUAL_SCENE_API TcVisualScene3D {
    public:
        TcVisualScene3D() = default;
        explicit TcVisualScene3D(tc_visual_scene3d_handle scene)
            : handle_(scene) {}

        std::optional<VisualItem3DHandle>
        adopt(tc_visual_item3d* item, tc_visual_item3d_deleter deleter, tc_visual_item3d* parent = nullptr);
        std::optional<VisualItem3DHandle> adopt(std::unique_ptr<VisualItem3D> item, VisualItem3D* parent = nullptr);
        bool replace(VisualItem3DHandle handle, tc_visual_item3d* replacement, tc_visual_item3d_deleter deleter);
        bool replace(VisualItem3DHandle handle, std::unique_ptr<VisualItem3D> replacement);
        bool destroy(VisualItem3DHandle handle);
        void clear();

        tc_visual_item3d* resolve(VisualItem3DHandle handle);
        const tc_visual_item3d* resolve(VisualItem3DHandle handle) const;
        std::vector<tc_visual_item3d*> items();

        termin::Affine3d world_transform(const tc_visual_item3d& item) const;
        bool effective_visible(const tc_visual_item3d& item) const;
        bool effective_enabled(const tc_visual_item3d& item) const;

        std::size_t size() const {
            return tc_visual_scene3d_item_count(handle_);
        }
        bool contains(VisualItem3DHandle handle) const {
            return resolve(handle) != nullptr;
        }
        std::uint64_t id() const {
            return tc_visual_scene3d_id(handle_);
        }
        tc_visual_scene3d_handle handle() const {
            return handle_;
        }
        bool valid() const {
            return tc_visual_scene3d_is_valid(handle_);
        }

    private:
        bool owns_(const tc_visual_item3d& item) const;

        tc_visual_scene3d_handle handle_ = tc_visual_scene3d_handle_invalid();
    };

} // namespace termin::visual
