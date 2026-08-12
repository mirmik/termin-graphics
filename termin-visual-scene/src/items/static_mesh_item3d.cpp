#include "termin_visual_scene/items/static_mesh_item3d.hpp"

#include "item_geometry3d_internal.hpp"

#include <stdexcept>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        bool valid_mesh(const termin::Mesh3& mesh) {
            if (mesh.vertices.empty() || mesh.triangles.empty() || mesh.triangles.size() % 3 != 0)
                return false;
            if (!std::all_of(mesh.vertices.begin(), mesh.vertices.end(), [](termin::Vec3f vertex) {
                    return detail::finite(vertex);
                }))
                return false;
            return std::all_of(mesh.triangles.begin(), mesh.triangles.end(), [&](std::uint32_t index) {
                return index < mesh.vertices.size();
            });
        }

        void require_mesh(const std::shared_ptr<const termin::Mesh3>& mesh) {
            if (!mesh || !valid_mesh(*mesh)) {
                tc::Log::error("StaticMeshItem3D rejected invalid mesh replacement");
                throw std::invalid_argument("StaticMeshItem3D requires a finite indexed triangle mesh");
            }
        }

        void require_tint(termin::LinearColor tint) {
            if (!detail::finite(tint)) {
                tc::Log::error("StaticMeshItem3D rejected a non-finite tint");
                throw std::invalid_argument("StaticMeshItem3D tint must be finite");
            }
        }

        void require_texture(const std::shared_ptr<const BaseColorTextureData3D>& texture) {
            const std::uint64_t expected = texture
                                               ? static_cast<std::uint64_t>(texture->width) * texture->height * 4u
                                               : 0u;
            if (!texture || texture->width == 0 || texture->height == 0 ||
                expected != texture->rgba8.size()) {
                tc::Log::error("StaticMeshItem3D rejected invalid base-color texture replacement");
                throw std::invalid_argument("StaticMeshItem3D requires a non-empty RGBA8 base-color texture");
            }
        }

    } // namespace

    StaticMeshItem3D::StaticMeshItem3D(std::shared_ptr<const termin::Mesh3> mesh,
                                       termin::LinearColor tint,
                                       bool depth_test)
        : NativeVisualItem3D("termin.visual.StaticMesh3D"),
          depth_test_(depth_test) {
        set_mesh(std::move(mesh));
        set_tint(tint);
    }

    void StaticMeshItem3D::set_mesh(std::shared_ptr<const termin::Mesh3> mesh) {
        require_mesh(mesh);
        if (base_color_texture_ && !mesh->has_uvs()) {
            tc::Log::error("StaticMeshItem3D rejected a mesh without UVs while a base-color texture is set");
            throw std::invalid_argument("StaticMeshItem3D textured mesh requires one UV per vertex");
        }
        mesh_ = std::move(mesh);
    }

    void StaticMeshItem3D::set_tint(termin::LinearColor tint) {
        require_tint(tint);
        tint_ = tint;
    }

    void StaticMeshItem3D::set_base_color_texture(std::shared_ptr<const BaseColorTextureData3D> texture) {
        require_texture(texture);
        if (!mesh_->has_uvs()) {
            tc::Log::error("StaticMeshItem3D rejected a base-color texture for a mesh without UVs");
            throw std::invalid_argument("StaticMeshItem3D textured mesh requires one UV per vertex");
        }
        base_color_texture_ = std::move(texture);
    }

    std::optional<VisualBounds3D> StaticMeshItem3D::local_bounds() const {
        return detail::bounds_of(mesh_->vertices.size(), [&](std::size_t index) { return mesh_->vertices[index]; });
    }

    std::optional<HitCandidate3D> StaticMeshItem3D::hit_test(const HitTestContext3D& context) const {
        return detail::ray_triangles(
            context.local_ray, mesh_->vertices.size(), mesh_->triangles, {}, [&](std::size_t index) {
                return mesh_->vertices[index];
            });
    }

    bool StaticMeshItem3D::paint(GraphicItemPaintContext3D& context) const {
        const StaticMeshDrawPacket3D packet{mesh_, base_color_texture_, tint_, depth_test_};
        return context.submit(StaticMeshDrawProtocol3D, &packet, sizeof(packet));
    }

} // namespace termin::visual
