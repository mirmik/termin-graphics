#include "termin_visual_scene/items/primitive_item3d.hpp"

#include "item_geometry3d_internal.hpp"

#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        bool valid_geometry(const PrimitiveGeometry3D& geometry) {
            if (geometry.vertices.empty() || geometry.triangles.empty() || geometry.triangles.size() % 3 != 0 ||
                (!geometry.triangle_parts.empty() && geometry.triangle_parts.size() != geometry.triangles.size() / 3)) {
                return false;
            }
            for (const auto& vertex : geometry.vertices) {
                if (!detail::finite(vertex.position) || !detail::finite(vertex.color))
                    return false;
            }
            return std::all_of(geometry.triangles.begin(), geometry.triangles.end(), [&](std::uint32_t index) {
                return index < geometry.vertices.size();
            });
        }

        void require_geometry(const std::shared_ptr<const PrimitiveGeometry3D>& geometry) {
            if (!geometry || !valid_geometry(*geometry)) {
                tc::Log::error("PrimitiveItem3D rejected invalid geometry replacement");
                throw std::invalid_argument("PrimitiveItem3D requires finite indexed triangle geometry");
            }
        }

    } // namespace

    PrimitiveItem3D::PrimitiveItem3D(std::shared_ptr<const PrimitiveGeometry3D> geometry, bool depth_test)
        : NativeVisualItem3D("termin.visual.Primitive3D"),
          depth_test_(depth_test) {
        set_geometry(std::move(geometry));
    }

    void PrimitiveItem3D::set_geometry(std::shared_ptr<const PrimitiveGeometry3D> geometry) {
        require_geometry(geometry);
        geometry_ = std::move(geometry);
    }

    std::optional<VisualBounds3D> PrimitiveItem3D::local_bounds() const {
        return detail::bounds_of(geometry_->vertices.size(),
                                 [&](std::size_t index) { return geometry_->vertices[index].position; });
    }

    std::optional<HitCandidate3D> PrimitiveItem3D::hit_test(const HitTestContext3D& context) const {
        return detail::ray_triangles(context.local_ray,
                                     geometry_->vertices.size(),
                                     geometry_->triangles,
                                     geometry_->triangle_parts,
                                     [&](std::size_t index) { return geometry_->vertices[index].position; });
    }

    bool PrimitiveItem3D::paint(GraphicItemPaintContext3D& context) const {
        const PrimitiveDrawPacket3D packet{geometry_, depth_test_};
        return context.submit(PrimitiveDrawProtocol3D, &packet, sizeof(packet));
    }

} // namespace termin::visual
