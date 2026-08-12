#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "termin_visual_scene/items/group_item3d.hpp"
#include "termin_visual_scene/items/point_cloud_item3d.hpp"
#include "termin_visual_scene/items/primitive_item3d.hpp"
#include "termin_visual_scene/items/static_mesh_item3d.hpp"

namespace {

    bool close(double left, double right) {
        return std::abs(left - right) < 1.0e-9;
    }

    std::shared_ptr<termin::visual::PrimitiveGeometry3D> make_primitive(std::uint64_t part = 77) {
        auto geometry = std::make_shared<termin::visual::PrimitiveGeometry3D>();
        geometry->vertices = {
            {{0.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
            {{0.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
            {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        };
        geometry->triangles = {0, 1, 2};
        geometry->triangle_parts = {part};
        return geometry;
    }

    std::shared_ptr<termin::Mesh3> make_mesh(float extent = 1.0f) {
        auto mesh = std::make_shared<termin::Mesh3>(
            std::vector<termin::Vec3f>{{0.0f, -extent, -extent}, {0.0f, extent, -extent}, {0.0f, 0.0f, extent}},
            std::vector<std::uint32_t>{0, 1, 2},
            "visual-scene-test");
        mesh->uvs = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}};
        return mesh;
    }

    std::shared_ptr<termin::visual::PointCloudData3D> make_cloud(float x = 0.0f) {
        auto cloud = std::make_shared<termin::visual::PointCloudData3D>();
        cloud->points.push_back({{x, 0.0f, 0.0f}, 1.0f, {1.0f, 1.0f, 1.0f, 1.0f}});
        return cloud;
    }

    tc_mat44 identity_matrix() {
        tc_mat44 result{};
        result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0;
        return result;
    }

    struct DrawRecord {
        std::string protocol;
        termin::Affine3d world_from_local = termin::Affine3d::identity();
        bool enabled = false;
        std::shared_ptr<const void> resource;
        std::shared_ptr<const termin::visual::BaseColorTextureData3D> base_color_texture;
    };

    class PacketSink final : public termin::visual::ScenePaintSink3D {
    public:
        bool begin(const termin::visual::VisualView3D&) override {
            staged.clear();
            return true;
        }

        bool submit(const termin::visual::DrawSubmission3D& submission) override {
            DrawRecord record;
            record.protocol = submission.packet.protocol;
            record.world_from_local = submission.world_from_local;
            record.enabled = submission.effective_enabled;
            if (record.protocol == termin::visual::PrimitiveDrawProtocol3D) {
                assert(submission.packet.payload_size == sizeof(termin::visual::PrimitiveDrawPacket3D));
                const auto& packet =
                    *static_cast<const termin::visual::PrimitiveDrawPacket3D*>(submission.packet.payload);
                record.resource = packet.geometry;
            } else if (record.protocol == termin::visual::StaticMeshDrawProtocol3D) {
                assert(submission.packet.payload_size == sizeof(termin::visual::StaticMeshDrawPacket3D));
                const auto& packet =
                    *static_cast<const termin::visual::StaticMeshDrawPacket3D*>(submission.packet.payload);
                record.resource = packet.mesh;
                record.base_color_texture = packet.base_color_texture;
            } else if (record.protocol == termin::visual::PointCloudDrawProtocol3D) {
                assert(submission.packet.payload_size == sizeof(termin::visual::PointCloudDrawPacket3D));
                const auto& packet =
                    *static_cast<const termin::visual::PointCloudDrawPacket3D*>(submission.packet.payload);
                record.resource = packet.cloud;
            } else {
                return false;
            }
            staged.push_back(std::move(record));
            return true;
        }

        bool end() override {
            published = staged;
            staged.clear();
            return true;
        }

        void abort() override {
            staged.clear();
        }

        std::vector<DrawRecord> staged;
        std::vector<DrawRecord> published;
    };

    class ThrowingItem final : public termin::visual::NativeVisualItem3D {
    public:
        ThrowingItem()
            : NativeVisualItem3D("termin.visual.test.ThrowingItem3D") {}

        std::optional<termin::visual::VisualBounds3D> local_bounds() const override {
            throw std::runtime_error("expected bounds failure");
        }
        std::optional<termin::visual::HitCandidate3D> hit_test(const termin::visual::HitTestContext3D&) const override {
            throw std::runtime_error("expected hit failure");
        }
        bool paint(termin::visual::GraphicItemPaintContext3D&) const override {
            throw std::runtime_error("expected paint failure");
        }
    };

} // namespace

int main() {
    using termin::Affine3d;
    using termin::visual::GroupItem3D;
    using termin::visual::PointCloudItem3D;
    using termin::visual::PrimitiveItem3D;
    using termin::visual::StaticMeshItem3D;
    using termin::visual::TcVisualScene3D;

    const auto scene_handle = tc_visual_scene3d_create();
    TcVisualScene3D scene(scene_handle);

    auto group = std::make_unique<GroupItem3D>();
    auto* group_ptr = group.get();
    group_ptr->set_local_transform(Affine3d::from_translation(1.0, 0.0, 0.0));
    assert(scene.adopt(std::move(group)));

    auto primitive_resource = make_primitive();
    std::weak_ptr<const termin::visual::PrimitiveGeometry3D> old_primitive_resource = primitive_resource;
    auto primitive = std::make_unique<PrimitiveItem3D>(primitive_resource);
    auto* primitive_ptr = primitive.get();
    primitive_ptr->set_local_transform(Affine3d::from_translation(2.0, 0.0, 0.0));
    const auto primitive_handle = scene.adopt(std::move(primitive), group_ptr);
    assert(primitive_handle);
    primitive_resource.reset();
    assert(!old_primitive_resource.expired());

    auto mesh_resource = make_mesh();
    std::weak_ptr<const termin::Mesh3> mesh_lifetime = mesh_resource;
    auto mesh = std::make_unique<StaticMeshItem3D>(mesh_resource);
    auto* mesh_ptr = mesh.get();
    mesh_ptr->set_local_transform(Affine3d::from_translation(5.0, 0.0, 0.0));
    const auto mesh_handle = scene.adopt(std::move(mesh));
    assert(mesh_handle);
    mesh_resource.reset();
    assert(!mesh_lifetime.expired());

    auto cloud_resource = make_cloud();
    std::weak_ptr<const termin::visual::PointCloudData3D> cloud_lifetime = cloud_resource;
    auto cloud = std::make_unique<PointCloudItem3D>(cloud_resource, tgfx::PointCloudStyle{}, 0.25);
    auto* cloud_ptr = cloud.get();
    cloud_ptr->set_local_transform(Affine3d::from_translation(7.0, 0.0, 0.0));
    const auto cloud_handle = scene.adopt(std::move(cloud));
    assert(cloud_handle);
    cloud_resource.reset();
    assert(!cloud_lifetime.expired());

    const termin::Ray3 ray{{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};
    auto hit = termin::visual::hit_test(scene, ray);
    assert(hit && tc_visual_item3d_handle_eq(hit->item, *primitive_handle));
    assert(close(hit->distance, 3.0) && hit->part == 77);

    primitive_ptr->set_enabled(false);
    hit = termin::visual::hit_test(scene, ray);
    assert(hit && tc_visual_item3d_handle_eq(hit->item, *mesh_handle));
    assert(close(hit->distance, 5.0) && hit->part == 1);
    mesh_ptr->set_enabled(false);
    hit = termin::visual::hit_test(scene, ray);
    assert(hit && tc_visual_item3d_handle_eq(hit->item, *cloud_handle));
    assert(close(hit->distance, 6.75) && hit->part == 1);

    tc_visual_bounds3d bounds{};
    assert(tc_visual_item3d_local_bounds_in_scene(scene_handle, *primitive_handle, &bounds));
    assert(bounds.min.y == -1.0 && bounds.max.z == 1.0);
    assert(tc_visual_item3d_local_bounds_in_scene(scene_handle, *cloud_handle, &bounds));
    assert(close(bounds.min.x, -0.25) && close(bounds.max.x, 0.25));

    termin::visual::VisualView3D view{
        .view_matrix = identity_matrix(),
        .projection_matrix = identity_matrix(),
        .camera_world_position = {0.0, 0.0, 0.0},
        .viewport_width = 320,
        .viewport_height = 240,
    };
    PacketSink sink;
    assert(termin::visual::paint(scene, view, sink));
    assert(sink.published.size() == 3);
    assert(sink.published[0].protocol == termin::visual::PrimitiveDrawProtocol3D);
    assert(close(sink.published[0].world_from_local.translation.x, 3.0));
    assert(!sink.published[0].enabled);
    assert(sink.published[1].protocol == termin::visual::StaticMeshDrawProtocol3D);
    assert(sink.published[2].protocol == termin::visual::PointCloudDrawProtocol3D);

    auto texture = std::make_shared<termin::visual::BaseColorTextureData3D>();
    texture->width = 2;
    texture->height = 1;
    texture->rgba8 = {255, 0, 0, 255, 0, 255, 0, 255};
    std::weak_ptr<const termin::visual::BaseColorTextureData3D> texture_lifetime = texture;
    mesh_ptr->set_base_color_texture(texture);
    texture.reset();
    assert(!texture_lifetime.expired());
    assert(termin::visual::paint(scene, view, sink));
    mesh_ptr->clear_base_color_texture();
    assert(!texture_lifetime.expired());
    assert(termin::visual::paint(scene, view, sink));
    assert(texture_lifetime.expired());

    group_ptr->set_visible(false);
    assert(termin::visual::paint(scene, view, sink));
    assert(sink.published.size() == 2);
    group_ptr->set_visible(true);

    // Replacement is transactional from the caller's perspective: invalid
    // resources leave the previous resource untouched and log the rejection.
    bool rejected = false;
    try {
        auto invalid = make_mesh();
        invalid->triangles[0] = 100;
        mesh_ptr->set_mesh(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected && mesh_ptr->mesh() == mesh_lifetime.lock());

    auto replacement_resource = make_primitive(99);
    primitive_ptr->set_geometry(replacement_resource);
    assert(old_primitive_resource.expired());
    std::weak_ptr<const termin::visual::PrimitiveGeometry3D> replacement_lifetime = replacement_resource;
    replacement_resource.reset();
    assert(!replacement_lifetime.expired());

    primitive_ptr->set_enabled(true);
    hit = termin::visual::hit_test(scene, ray);
    assert(hit && hit->part == 99);

    // Sink-held packet resources survive item destruction until the completed
    // frame is released.
    assert(termin::visual::paint(scene, view, sink));
    assert(scene.destroy(*primitive_handle));
    assert(!replacement_lifetime.expired());
    sink.published.clear();
    assert(replacement_lifetime.expired());

    scene.clear();
    assert(mesh_lifetime.expired());
    assert(cloud_lifetime.expired());

    const auto throwing_handle = scene.adopt(std::make_unique<ThrowingItem>());
    assert(throwing_handle);
    assert(!tc_visual_item3d_local_bounds_in_scene(scene_handle, *throwing_handle, &bounds));
    assert(!termin::visual::hit_test(scene, ray));
    assert(!termin::visual::paint(scene, view, sink));
    scene.clear();
    tc_visual_scene3d_destroy(scene_handle);
}
