#include <cstring>
#include <memory>

#include "termin_visual_scene/items/point_cloud_item3d.hpp"
#include "termin_visual_scene/items/primitive_item3d.hpp"

namespace {

    class ExampleSink final : public termin::visual::ScenePaintSink3D {
    public:
        bool begin(const termin::visual::VisualView3D&) override {
            staged = 0;
            return true;
        }
        bool submit(const termin::visual::DrawSubmission3D& submission) override {
            if (std::strcmp(submission.packet.protocol, termin::visual::PrimitiveDrawProtocol3D) != 0 &&
                std::strcmp(submission.packet.protocol, termin::visual::PointCloudDrawProtocol3D) != 0) {
                return false;
            }
            ++staged;
            return true;
        }
        bool end() override {
            published = staged;
            return true;
        }
        void abort() override {
            staged = 0;
        }

        std::size_t staged = 0;
        std::size_t published = 0;
    };

    tc_mat44 identity_matrix() {
        tc_mat44 result{};
        result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0;
        return result;
    }

} // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::strcmp(argv[1], "--headless-smoke") != 0)
        return 64;

    auto primitive_data = std::make_shared<termin::visual::PrimitiveGeometry3D>();
    primitive_data->vertices = {
        {{1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{1.0f, 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    primitive_data->triangles = {0, 1, 2};

    auto cloud_data = std::make_shared<termin::visual::PointCloudData3D>();
    cloud_data->points.push_back({{3.0f, 0.0f, 0.0f}, 1.0f, {1.0f, 1.0f, 1.0f, 1.0f}});

    const auto scene_handle = tc_visual_scene3d_create();
    termin::visual::TcVisualScene3D scene(scene_handle);
    if (!scene.adopt(std::make_unique<termin::visual::PrimitiveItem3D>(primitive_data)) ||
        !scene.adopt(std::make_unique<termin::visual::PointCloudItem3D>(cloud_data))) {
        tc_visual_scene3d_destroy(scene_handle);
        return 1;
    }

    const termin::visual::VisualView3D view{
        .view_matrix = identity_matrix(),
        .projection_matrix = identity_matrix(),
        .camera_world_position = {0.0, 0.0, 0.0},
        .viewport_width = 320,
        .viewport_height = 240,
    };
    ExampleSink sink;
    const bool painted = termin::visual::paint(scene, view, sink);
    const auto hit = termin::visual::hit_test(scene, {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}});
    const bool ok = painted && sink.published == 2 && hit.has_value();
    scene.clear();
    tc_visual_scene3d_destroy(scene_handle);
    return ok ? 0 : 2;
}
