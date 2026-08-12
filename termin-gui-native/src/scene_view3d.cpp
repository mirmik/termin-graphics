#include <termin/gui_native/scene_view3d.hpp>

#include "widgets_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <termin/geom/mat44.hpp>
#include <termin_visual_scene/items/item3d_packets.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/builtin_shader_sources.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/immediate_renderer.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/tc_shader_bridge.hpp>
#include <tgfx2/vertex_layout.hpp>

extern "C" {
#include <tgfx/resources/tc_shader.h>
#include <tgfx/resources/tc_shader_registry.h>
}

namespace termin::gui_native {
    namespace {

        constexpr termin::visual::PointerId3D kMousePointer = 1;
        constexpr const char* kTexturedStaticMeshShader = "termin-engine-static-mesh-textured";

        struct TexturedStaticMeshVertex {
            termin::Vec3f position;
            termin::Vec2f uv;
        };

        struct TexturedStaticMeshPush {
            float view_projection[16];
            float base_color_factor[4];
        };

        tgfx::VertexLayoutDesc textured_static_mesh_layout() {
            tgfx::VertexLayoutDesc layout;
            layout.stride = sizeof(TexturedStaticMeshVertex);
            layout.attribute_count = 2;
            layout.attributes[0] = {0,
                                    tgfx::VertexFormat::Float3,
                                    static_cast<std::uint32_t>(offsetof(TexturedStaticMeshVertex, position)),
                                    tgfx::intern_vertex_semantic("position")};
            layout.attributes[1] = {1,
                                    tgfx::VertexFormat::Float2,
                                    static_cast<std::uint32_t>(offsetof(TexturedStaticMeshVertex, uv)),
                                    tgfx::intern_vertex_semantic("uv0")};
            return layout;
        }

        bool finite_matrix(const tc_mat44& matrix) {
            return std::all_of(
                std::begin(matrix.m), std::end(matrix.m), [](double value) { return std::isfinite(value); });
        }

        bool finite_camera(const SceneView3DCamera& camera) {
            return finite_matrix(camera.view_matrix) && finite_matrix(camera.projection_matrix) &&
                   std::isfinite(camera.world_position.x) && std::isfinite(camera.world_position.y) &&
                   std::isfinite(camera.world_position.z);
        }

        termin::Mat44 to_matrix(const tc_mat44& source) {
            termin::Mat44 result;
            std::copy(std::begin(source.m), std::end(source.m), std::begin(result.data));
            return result;
        }

        tc_mat44 identity_matrix() {
            tc_mat44 result{};
            result.m[0] = 1.0;
            result.m[5] = 1.0;
            result.m[10] = 1.0;
            result.m[15] = 1.0;
            return result;
        }

        bool same_camera(const SceneView3DCamera& left, const SceneView3DCamera& right) {
            return std::memcmp(&left, &right, sizeof(SceneView3DCamera)) == 0;
        }

        ViewportSurfaceSize pixel_size(tc_ui_rect rect) {
            if (!std::isfinite(rect.width) || !std::isfinite(rect.height) || rect.width <= 0.0f ||
                rect.height <= 0.0f) {
                return {};
            }
            const double width = std::round(static_cast<double>(rect.width));
            const double height = std::round(static_cast<double>(rect.height));
            if (width <= 0.0 || height <= 0.0) {
                return {};
            }
            const double maximum = static_cast<double>(std::numeric_limits<int>::max());
            return {static_cast<int>(std::min(width, maximum)), static_cast<int>(std::min(height, maximum))};
        }

        bool invert_matrix(const termin::Mat44& matrix, termin::Mat44& inverse) {
            double rows[4][8]{};
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    rows[row][column] = matrix(column, row);
                }
                rows[row][row + 4] = 1.0;
            }
            for (int pivot = 0; pivot < 4; ++pivot) {
                int selected = pivot;
                for (int row = pivot + 1; row < 4; ++row) {
                    if (std::abs(rows[row][pivot]) > std::abs(rows[selected][pivot])) {
                        selected = row;
                    }
                }
                if (!std::isfinite(rows[selected][pivot]) || std::abs(rows[selected][pivot]) <= 1.0e-12) {
                    return false;
                }
                if (selected != pivot) {
                    for (int column = 0; column < 8; ++column) {
                        std::swap(rows[selected][column], rows[pivot][column]);
                    }
                }
                const double divisor = rows[pivot][pivot];
                for (double& value : rows[pivot]) {
                    value /= divisor;
                }
                for (int row = 0; row < 4; ++row) {
                    if (row == pivot)
                        continue;
                    const double factor = rows[row][pivot];
                    for (int column = 0; column < 8; ++column) {
                        rows[row][column] -= factor * rows[pivot][column];
                    }
                }
            }
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    inverse(column, row) = rows[row][column + 4];
                }
            }
            return true;
        }

        bool unproject(const termin::Mat44& inverse, double x, double y, double z, termin::Vec3& result) {
            const double px = inverse(0, 0) * x + inverse(1, 0) * y + inverse(2, 0) * z + inverse(3, 0);
            const double py = inverse(0, 1) * x + inverse(1, 1) * y + inverse(2, 1) * z + inverse(3, 1);
            const double pz = inverse(0, 2) * x + inverse(1, 2) * y + inverse(2, 2) * z + inverse(3, 2);
            const double pw = inverse(0, 3) * x + inverse(1, 3) * y + inverse(2, 3) * z + inverse(3, 3);
            if (!std::isfinite(pw) || std::abs(pw) <= 1.0e-12) {
                return false;
            }
            result = {px / pw, py / pw, pz / pw};
            return std::isfinite(result.x) && std::isfinite(result.y) && std::isfinite(result.z);
        }

        struct CollectedDraw {
            tc_visual_item3d_handle item = tc_visual_item3d_handle_invalid();
            termin::Affine3d world_from_local = termin::Affine3d::identity();
            std::string protocol;
            termin::visual::PrimitiveDrawPacket3D primitive;
            termin::visual::StaticMeshDrawPacket3D mesh;
            termin::visual::PointCloudDrawPacket3D point_cloud;
        };

        class CollectingSink final : public termin::visual::ScenePaintSink3D {
        public:
            std::vector<CollectedDraw> draws;

            bool begin(const termin::visual::VisualView3D&) override {
                draws.clear();
                return true;
            }

            bool submit(const termin::visual::DrawSubmission3D& submission) override {
                if (!submission.packet.protocol || !submission.packet.payload) {
                    tc_log_error("[termin-gui-native] SceneView3D received an empty draw packet");
                    return false;
                }
                CollectedDraw draw;
                draw.item = submission.item;
                draw.world_from_local = submission.world_from_local;
                draw.protocol = submission.packet.protocol;
                if (draw.protocol == termin::visual::PrimitiveDrawProtocol3D &&
                    submission.packet.payload_size == sizeof(draw.primitive)) {
                    draw.primitive =
                        *static_cast<const termin::visual::PrimitiveDrawPacket3D*>(submission.packet.payload);
                } else if (draw.protocol == termin::visual::StaticMeshDrawProtocol3D &&
                           submission.packet.payload_size == sizeof(draw.mesh)) {
                    draw.mesh = *static_cast<const termin::visual::StaticMeshDrawPacket3D*>(submission.packet.payload);
                } else if (draw.protocol == termin::visual::PointCloudDrawProtocol3D &&
                           submission.packet.payload_size == sizeof(draw.point_cloud)) {
                    draw.point_cloud =
                        *static_cast<const termin::visual::PointCloudDrawPacket3D*>(submission.packet.payload);
                } else {
                    tc_log_error("[termin-gui-native] SceneView3D has no renderer for protocol '%s'",
                                 submission.packet.protocol);
                    return false;
                }
                draws.push_back(std::move(draw));
                return true;
            }

            bool end() override {
                return true;
            }

            void abort() override {
                draws.clear();
            }
        };

        void append_linear_vertex(std::vector<float>& destination, termin::Vec3 position, termin::LinearColor color) {
            destination.push_back(static_cast<float>(position.x));
            destination.push_back(static_cast<float>(position.y));
            destination.push_back(static_cast<float>(position.z));
            destination.push_back(color.r);
            destination.push_back(color.g);
            destination.push_back(color.b);
            destination.push_back(color.a);
        }

    } // namespace

    struct SceneView3D::RenderState {
        struct PointCloudCache {
            std::shared_ptr<const termin::visual::PointCloudData3D> source;
            termin::Affine3d transform = termin::Affine3d::identity();
            std::unique_ptr<tgfx::PointCloud> gpu;
            bool used = false;
        };

        struct BaseColorTextureCache {
            std::shared_ptr<const termin::visual::BaseColorTextureData3D> source;
            tgfx::TextureHandle gpu{};
            bool used = false;
        };

        tgfx::IRenderDevice* device = nullptr;
        tgfx::TextureHandle color{};
        tgfx::TextureHandle depth{};
        termin::ImmediateRenderer immediate;
        tgfx::PointCloudRenderer point_renderer;
        std::vector<PointCloudCache> point_clouds;
        std::vector<BaseColorTextureCache> base_color_textures;
        tc_shader_handle textured_mesh_shader = tc_shader_handle_invalid();
        tgfx::ShaderHandle textured_mesh_vertex{};
        tgfx::ShaderHandle textured_mesh_fragment{};
    };

    SceneView3D::SceneView3D(termin::visual::TcVisualScene3D scene)
        : NativeWidget("SceneView3D"),
          scene_(scene.handle()),
          render_state_(std::make_unique<RenderState>()) {
        camera_.view_matrix = identity_matrix();
        camera_.projection_matrix = identity_matrix();
        set_style_role(TC_UI_STYLE_PANEL);
        set_focusable(true);
        set_preferred_size(tc_ui_size{320.0f, 200.0f});
    }

    SceneView3D::~SceneView3D() {
        release_render_resources();
    }

    termin::visual::TcVisualScene3D SceneView3D::scene() const {
        return termin::visual::TcVisualScene3D{scene_};
    }

    void SceneView3D::set_scene(termin::visual::TcVisualScene3D scene_value) {
        if (tc_visual_scene3d_handle_eq(scene_, scene_value.handle()))
            return;
        if (scene().valid()) {
            interaction_.cancel_all(scene());
        }
        if ((scene_pointer_active_ || fallback_pointer_active_) && tc_ui_document_is_valid(document())) {
            if (tc_widget_handle_eq(tc_ui_document_pointer_capture(document()), handle())) {
                tc_ui_document_release_pointer_capture(document(), handle());
            }
        }
        interaction_.cancel_all();
        scene_pointer_active_ = false;
        fallback_pointer_active_ = false;
        scene_ = scene_value.handle();
        invalidate_scene();
    }

    void SceneView3D::invalidate_scene() {
        render_dirty_ = true;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }

    void SceneView3D::set_camera(SceneView3DCamera camera_value) {
        if (!finite_camera(camera_value)) {
            tc_log_error("[termin-gui-native] SceneView3D rejected a non-finite camera");
            return;
        }
        if (same_camera(camera_, camera_value) && !camera_provider_)
            return;
        camera_provider_ = {};
        camera_ = camera_value;
        invalidate_view();
    }

    const SceneView3DCamera& SceneView3D::camera() const {
        return camera_;
    }

    void SceneView3D::set_camera_provider(CameraProvider provider) {
        camera_provider_ = std::move(provider);
        invalidate_view();
    }

    void SceneView3D::invalidate_view() {
        render_dirty_ = true;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }

    ViewportSurfaceSize SceneView3D::framebuffer_size() const {
        return requested_size_;
    }

    uint32_t SceneView3D::texture_id() const {
        return render_state_ && render_state_->color ? render_state_->color.id : 0;
    }

    std::optional<termin::Ray3> SceneView3D::world_ray(float widget_x, float widget_y) const {
        if (requested_size_.width <= 0 || requested_size_.height <= 0 || !finite_camera(camera_)) {
            return std::nullopt;
        }
        const tc_ui_rect rect = bounds();
        if (rect.width <= 0.0f || rect.height <= 0.0f) {
            return std::nullopt;
        }
        const double pixel_x = static_cast<double>(widget_x - rect.x) * requested_size_.width / rect.width;
        const double pixel_y = static_cast<double>(widget_y - rect.y) * requested_size_.height / rect.height;
        const double ndc_x = pixel_x / requested_size_.width * 2.0 - 1.0;
        const double ndc_y = pixel_y / requested_size_.height * 2.0 - 1.0;
        termin::Mat44 inverse;
        if (!invert_matrix(to_matrix(camera_.projection_matrix) * to_matrix(camera_.view_matrix), inverse)) {
            tc_log_error("[termin-gui-native] SceneView3D cannot unproject through a singular camera");
            return std::nullopt;
        }
        termin::Vec3 near_point;
        termin::Vec3 far_point;
        if (!unproject(inverse, ndc_x, ndc_y, 0.0, near_point) || !unproject(inverse, ndc_x, ndc_y, 1.0, far_point)) {
            tc_log_error("[termin-gui-native] SceneView3D produced an invalid world ray");
            return std::nullopt;
        }
        const termin::Vec3 delta = far_point - near_point;
        const double length = delta.norm();
        if (!std::isfinite(length) || length <= 1.0e-12) {
            tc_log_error("[termin-gui-native] SceneView3D produced a degenerate world ray");
            return std::nullopt;
        }
        return termin::Ray3{near_point, delta / length};
    }

    termin::visual::SceneInteraction3D& SceneView3D::interaction() {
        return interaction_;
    }

    const termin::visual::SceneInteraction3D& SceneView3D::interaction() const {
        return interaction_;
    }

    void SceneView3D::set_fallback_pointer_handler(FallbackPointerHandler handler) {
        fallback_pointer_handler_ = std::move(handler);
    }

    void SceneView3D::set_clear_color(termin::LinearColor color) {
        if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) || !std::isfinite(color.a)) {
            tc_log_error("[termin-gui-native] SceneView3D rejected a non-finite clear color");
            return;
        }
        clear_color_ = color;
        invalidate_view();
    }

    tc_ui_size SceneView3D::measure(tc_ui_document_handle, tc_ui_constraints constraints) {
        return detail::clamp_size(preferred_size(), constraints);
    }

    bool SceneView3D::sync_framebuffer_size() {
        const ViewportSurfaceSize next = pixel_size(bounds());
        if (next == requested_size_)
            return false;
        requested_size_ = next;
        invalidate_view();
        return true;
    }

    void SceneView3D::layout(tc_ui_document_handle document, tc_ui_rect rect) {
        NativeWidget::layout(document, rect);
        sync_framebuffer_size();
    }

    void SceneView3D::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
        const tc_ui_style style = computed_style(document);
        tc_ui_painter_fill_rect(context, bounds(), style.background);
        if (texture_id() != 0 && requested_size_.width > 0 && requested_size_.height > 0) {
            tc_ui_painter_draw_texture(context,
                                       texture_id(),
                                       bounds(),
                                       tc_ui_srgb_color{1.0f, 1.0f, 1.0f, 1.0f},
                                       TC_UI_TEXTURE_SAMPLING_LINEAR,
                                       false);
        }
    }

    bool SceneView3D::call_fallback(const tc_ui_pointer_event& event, const std::optional<termin::Ray3>& ray) {
        if (!fallback_pointer_handler_)
            return false;
        try {
            return fallback_pointer_handler_(*this, event, ray);
        } catch (const std::exception& error) {
            tc_log_error("[termin-gui-native] SceneView3D fallback pointer handler failed: %s", error.what());
        } catch (...) {
            tc_log_error("[termin-gui-native] SceneView3D fallback pointer handler failed");
        }
        return false;
    }

    void SceneView3D::cancel_pointer(tc_ui_document_handle document, const tc_ui_pointer_event& event) {
        const auto ray = world_ray(event.x, event.y);
        if (scene_pointer_active_ && scene().valid() && ray) {
            interaction_.route(scene(),
                               {kMousePointer,
                                termin::visual::PointerEventKind3D::Cancel,
                                *ray,
                                static_cast<uint32_t>(std::max(active_button_, 0))});
        }
        if (fallback_pointer_active_) {
            call_fallback(event, ray);
        }
        interaction_.release(kMousePointer);
        scene_pointer_active_ = false;
        fallback_pointer_active_ = false;
        if (tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle())) {
            tc_ui_document_release_pointer_capture(document, handle());
        }
        invalidate_scene();
    }

    tc_ui_event_result SceneView3D::pointer_event(tc_ui_document_handle document, const tc_ui_pointer_event* event) {
        if (!event)
            return TC_UI_EVENT_IGNORED;
        if (scene_pointer_active_ && !scene().valid()) {
            tc_log_error("[termin-gui-native] SceneView3D cancelled pointer capture because its borrowed scene "
                         "is no longer valid");
            interaction_.cancel_all();
            scene_pointer_active_ = false;
            if (tc_widget_handle_eq(tc_ui_document_pointer_capture(document), handle())) {
                tc_ui_document_release_pointer_capture(document, handle());
            }
        }
        if (event->type == TC_UI_POINTER_CANCEL) {
            const bool active = scene_pointer_active_ || fallback_pointer_active_;
            cancel_pointer(document, *event);
            return active ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
        }

        const auto ray = world_ray(event->x, event->y);
        if (fallback_pointer_active_) {
            const bool handled = call_fallback(*event, ray);
            if (event->type == TC_UI_POINTER_UP) {
                fallback_pointer_active_ = false;
                tc_ui_document_release_pointer_capture(document, handle());
            }
            if (handled)
                invalidate_view();
            return TC_UI_EVENT_HANDLED;
        }

        std::optional<termin::visual::PointerEventKind3D> kind;
        if (event->type == TC_UI_POINTER_MOVE)
            kind = termin::visual::PointerEventKind3D::Move;
        else if (event->type == TC_UI_POINTER_DOWN)
            kind = termin::visual::PointerEventKind3D::Down;
        else if (event->type == TC_UI_POINTER_UP)
            kind = termin::visual::PointerEventKind3D::Up;

        if (kind && ray && scene().valid()) {
            const auto dispatch = interaction_.route(
                scene(), {kMousePointer, *kind, *ray, static_cast<uint32_t>(std::max(event->button, 0))});
            const bool routed_to_scene = !tc_visual_item3d_handle_is_invalid(dispatch.target);
            if (event->type == TC_UI_POINTER_DOWN && routed_to_scene) {
                scene_pointer_active_ = true;
                active_button_ = event->button;
                tc_ui_document_set_focus(document, handle());
                tc_ui_document_set_pointer_capture(document, handle());
            } else if (event->type == TC_UI_POINTER_UP && scene_pointer_active_) {
                scene_pointer_active_ = false;
                tc_ui_document_release_pointer_capture(document, handle());
            }
            invalidate_scene();
            if (routed_to_scene || scene_pointer_active_ || event->type == TC_UI_POINTER_UP) {
                return TC_UI_EVENT_HANDLED;
            }
        }

        const bool fallback_handled = call_fallback(*event, ray);
        if (fallback_handled) {
            if (event->type == TC_UI_POINTER_DOWN) {
                fallback_pointer_active_ = true;
                active_button_ = event->button;
                tc_ui_document_set_focus(document, handle());
                tc_ui_document_set_pointer_capture(document, handle());
            }
            invalidate_view();
            return TC_UI_EVENT_HANDLED;
        }
        return TC_UI_EVENT_IGNORED;
    }

    void SceneView3D::on_destroy(tc_ui_document_handle document) {
        tc_ui_pointer_event cancel{};
        cancel.type = TC_UI_POINTER_CANCEL;
        cancel.cancel_reason = TC_UI_POINTER_CANCEL_SUBTREE_INEFFECTIVE;
        cancel_pointer(document, cancel);
        fallback_pointer_handler_ = {};
        camera_provider_ = {};
        scene_ = tc_visual_scene3d_handle_invalid();
        release_render_resources();
        NativeWidget::on_destroy(document);
    }

    bool SceneView3D::update_camera_from_provider() {
        if (!camera_provider_)
            return true;
        try {
            const auto provided = camera_provider_(requested_size_);
            if (!provided) {
                tc_log_error("[termin-gui-native] SceneView3D camera provider returned no camera");
                return false;
            }
            if (!finite_camera(*provided)) {
                tc_log_error("[termin-gui-native] SceneView3D camera provider returned a non-finite camera");
                return false;
            }
            if (!same_camera(camera_, *provided)) {
                camera_ = *provided;
                render_dirty_ = true;
            }
            return true;
        } catch (const std::exception& error) {
            tc_log_error("[termin-gui-native] SceneView3D camera provider failed: %s", error.what());
        } catch (...) {
            tc_log_error("[termin-gui-native] SceneView3D camera provider failed");
        }
        return false;
    }

    void SceneView3D::prepare_render(tgfx::RenderContext2& context, tgfx::FontAtlas&, float) {
        if (requested_size_.width <= 0 || requested_size_.height <= 0 || !update_camera_from_provider())
            return;
        RenderState& state = *render_state_;
        tgfx::IRenderDevice& device = context.device();
        if (state.device && state.device != &device) {
            release_render_resources();
        }
        state.device = &device;

        const auto target_mismatch = [&](tgfx::TextureHandle texture) {
            if (!texture)
                return true;
            const tgfx::TextureDesc description = device.texture_desc(texture);
            return description.width != static_cast<uint32_t>(requested_size_.width) ||
                   description.height != static_cast<uint32_t>(requested_size_.height);
        };
        if (target_mismatch(state.color) || target_mismatch(state.depth)) {
            if (state.color)
                device.destroy(state.color);
            if (state.depth)
                device.destroy(state.depth);
            device.invalidate_render_target_cache();
            tgfx::TextureDesc color_description;
            color_description.width = static_cast<uint32_t>(requested_size_.width);
            color_description.height = static_cast<uint32_t>(requested_size_.height);
            color_description.format = tgfx::PixelFormat::RGBA16F;
            color_description.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::ColorAttachment;
            state.color = device.create_texture(color_description);
            tgfx::TextureDesc depth_description;
            depth_description.width = color_description.width;
            depth_description.height = color_description.height;
            depth_description.format = tgfx::PixelFormat::D32F;
            depth_description.usage = tgfx::TextureUsage::DepthStencilAttachment;
            state.depth = device.create_texture(depth_description);
            if (!state.color || !state.depth) {
                tc_log_error("[termin-gui-native] SceneView3D failed to allocate a %dx%d framebuffer",
                             requested_size_.width,
                             requested_size_.height);
                return;
            }
            render_dirty_ = true;
            mark_dirty(TC_WIDGET_DIRTY_PAINT);
        }
        if (!render_dirty_)
            return;

        termin::visual::VisualView3D view{};
        view.view_matrix = camera_.view_matrix;
        view.projection_matrix = camera_.projection_matrix;
        view.camera_world_position = camera_.world_position;
        view.viewport_width = static_cast<uint32_t>(requested_size_.width);
        view.viewport_height = static_cast<uint32_t>(requested_size_.height);
        CollectingSink sink;
        if (scene().valid() && !termin::visual::paint(scene(), view, sink)) {
            tc_log_error("[termin-gui-native] SceneView3D failed to collect a scene frame");
            return;
        }

        context.begin_pass(state.color, state.depth, &clear_color_, 1.0f, true);
        context.set_viewport(0, 0, requested_size_.width, requested_size_.height);
        const termin::Mat44 view_matrix = to_matrix(camera_.view_matrix);
        const termin::Mat44 projection_matrix = to_matrix(camera_.projection_matrix);
        const termin::Mat44 view_projection = projection_matrix * view_matrix;
        std::array<float, 16> view_projection_float{};
        for (size_t index = 0; index < view_projection_float.size(); ++index)
            view_projection_float[index] = static_cast<float>(view_projection.data[index]);
        for (auto& cache : state.point_clouds)
            cache.used = false;
        for (auto& cache : state.base_color_textures)
            cache.used = false;

        for (const CollectedDraw& draw : sink.draws) {
            if (draw.protocol == termin::visual::PrimitiveDrawProtocol3D && draw.primitive.geometry) {
                const auto& geometry = *draw.primitive.geometry;
                state.immediate.begin();
                auto& vertices =
                    draw.primitive.depth_test ? state.immediate.tri_vertices_depth : state.immediate.tri_vertices;
                for (size_t index = 0; index + 2 < geometry.triangles.size(); index += 3) {
                    const uint32_t indices[3] = {
                        geometry.triangles[index], geometry.triangles[index + 1], geometry.triangles[index + 2]};
                    if (indices[0] >= geometry.vertices.size() || indices[1] >= geometry.vertices.size() ||
                        indices[2] >= geometry.vertices.size()) {
                        tc_log_error("[termin-gui-native] SceneView3D skipped an invalid primitive triangle");
                        continue;
                    }
                    for (uint32_t vertex_index : indices) {
                        const auto& vertex = geometry.vertices[vertex_index];
                        append_linear_vertex(vertices,
                                             draw.world_from_local.transform_point(
                                                 {vertex.position.x, vertex.position.y, vertex.position.z}),
                                             vertex.color);
                    }
                }
                state.immediate.flush_depth(&context, view_matrix, projection_matrix, true);
                state.immediate.flush(&context, view_matrix, projection_matrix, false, true);
            } else if (draw.protocol == termin::visual::StaticMeshDrawProtocol3D && draw.mesh.mesh &&
                       draw.mesh.base_color_texture) {
                const auto& mesh = *draw.mesh.mesh;
                const auto& texture_source = draw.mesh.base_color_texture;
                if (!mesh.has_uvs()) {
                    tc_log_error("[termin-gui-native] SceneView3D skipped a textured mesh without UVs");
                    continue;
                }
                auto texture_cache =
                    std::find_if(state.base_color_textures.begin(), state.base_color_textures.end(), [&](const auto& value) {
                        return value.source == texture_source;
                    });
                if (texture_cache == state.base_color_textures.end()) {
                    tgfx::TextureDesc description;
                    description.width = texture_source->width;
                    description.height = texture_source->height;
                    description.format = tgfx::PixelFormat::RGBA8_sRGB;
                    description.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopyDst;
                    RenderState::BaseColorTextureCache created;
                    created.source = texture_source;
                    created.gpu = device.create_texture(description);
                    if (!created.gpu) {
                        tc_log_error("[termin-gui-native] SceneView3D failed to create a base-color texture");
                        continue;
                    }
                    device.upload_texture(created.gpu, texture_source->rgba8);
                    state.base_color_textures.push_back(std::move(created));
                    texture_cache = std::prev(state.base_color_textures.end());
                }
                texture_cache->used = true;

                if (tc_shader_handle_is_invalid(state.textured_mesh_shader))
                    state.textured_mesh_shader = tgfx::register_builtin_shader_from_catalog(kTexturedStaticMeshShader);
                tc_shader* shader = tc_shader_get(state.textured_mesh_shader);
                if (!shader || !termin::tc_shader_ensure_tgfx2(shader,
                                                               &device,
                                                               &state.textured_mesh_vertex,
                                                               &state.textured_mesh_fragment)) {
                    tc_log_error("[termin-gui-native] SceneView3D textured static-mesh shader is unavailable");
                    continue;
                }

                std::vector<TexturedStaticMeshVertex> vertices;
                vertices.reserve(mesh.triangles.size());
                for (std::uint32_t vertex_index : mesh.triangles) {
                    if (vertex_index >= mesh.vertices.size() || vertex_index >= mesh.uvs.size()) {
                        tc_log_error("[termin-gui-native] SceneView3D skipped an invalid textured mesh triangle");
                        vertices.clear();
                        break;
                    }
                    const auto& vertex = mesh.vertices[vertex_index];
                    const auto world = draw.world_from_local.transform_point({vertex.x, vertex.y, vertex.z});
                    vertices.push_back({{static_cast<float>(world.x),
                                         static_cast<float>(world.y),
                                         static_cast<float>(world.z)},
                                        mesh.uvs[vertex_index]});
                }
                if (vertices.empty())
                    continue;

                TexturedStaticMeshPush push{};
                std::copy(view_projection_float.begin(), view_projection_float.end(), push.view_projection);
                push.base_color_factor[0] = draw.mesh.tint.r;
                push.base_color_factor[1] = draw.mesh.tint.g;
                push.base_color_factor[2] = draw.mesh.tint.b;
                push.base_color_factor[3] = draw.mesh.tint.a;
                context.set_depth_test(draw.mesh.depth_test);
                context.set_depth_write(draw.mesh.depth_test);
                context.set_blend(false);
                context.set_cull(tgfx::CullMode::None);
                context.bind_shader(state.textured_mesh_vertex, state.textured_mesh_fragment);
                context.use_shader_resource_layout(shader);
                context.bind_uniform_data("u_push", &push, sizeof(push));
                context.bind_texture("u_base_color_texture", texture_cache->gpu);
                const auto layout = textured_static_mesh_layout();
                context.draw_transient_arrays(vertices.data(),
                                              static_cast<std::uint32_t>(vertices.size() * sizeof(vertices[0])),
                                              static_cast<std::uint32_t>(vertices.size()),
                                              layout,
                                              tgfx::PrimitiveTopology::TriangleList);
            } else if (draw.protocol == termin::visual::StaticMeshDrawProtocol3D && draw.mesh.mesh) {
                const auto& mesh = *draw.mesh.mesh;
                state.immediate.begin();
                auto& vertices =
                    draw.mesh.depth_test ? state.immediate.tri_vertices_depth : state.immediate.tri_vertices;
                for (size_t index = 0; index + 2 < mesh.triangles.size(); index += 3) {
                    const uint32_t indices[3] = {
                        mesh.triangles[index], mesh.triangles[index + 1], mesh.triangles[index + 2]};
                    if (indices[0] >= mesh.vertices.size() || indices[1] >= mesh.vertices.size() ||
                        indices[2] >= mesh.vertices.size()) {
                        tc_log_error("[termin-gui-native] SceneView3D skipped an invalid mesh triangle");
                        continue;
                    }
                    for (uint32_t vertex_index : indices) {
                        const auto& vertex = mesh.vertices[vertex_index];
                        append_linear_vertex(vertices,
                                             draw.world_from_local.transform_point({vertex.x, vertex.y, vertex.z}),
                                             draw.mesh.tint);
                    }
                }
                state.immediate.flush_depth(&context, view_matrix, projection_matrix, true);
                state.immediate.flush(&context, view_matrix, projection_matrix, false, true);
            } else if (draw.protocol == termin::visual::PointCloudDrawProtocol3D && draw.point_cloud.cloud) {
                auto cache = std::find_if(state.point_clouds.begin(), state.point_clouds.end(), [&](const auto& value) {
                    return value.source == draw.point_cloud.cloud &&
                           std::memcmp(&value.transform, &draw.world_from_local, sizeof(termin::Affine3d)) == 0;
                });
                if (cache == state.point_clouds.end()) {
                    RenderState::PointCloudCache created;
                    created.source = draw.point_cloud.cloud;
                    created.transform = draw.world_from_local;
                    created.gpu = std::make_unique<tgfx::PointCloud>();
                    std::vector<tgfx::PointCloudPoint> transformed = created.source->points;
                    for (auto& point : transformed) {
                        const termin::Vec3 world =
                            created.transform.transform_point({point.position.x, point.position.y, point.position.z});
                        point.position = {
                            static_cast<float>(world.x), static_cast<float>(world.y), static_cast<float>(world.z)};
                    }
                    if (!created.gpu->upload(context, transformed)) {
                        tc_log_error("[termin-gui-native] SceneView3D failed to upload a point cloud");
                        continue;
                    }
                    state.point_clouds.push_back(std::move(created));
                    cache = std::prev(state.point_clouds.end());
                }
                cache->used = true;
                tgfx::PointCloudDrawParams parameters;
                parameters.view_projection = view_projection_float;
                state.point_renderer.draw(context, *cache->gpu, draw.point_cloud.style, parameters);
            }
        }
        context.end_pass();

        std::erase_if(state.point_clouds, [&](auto& cache) {
            if (cache.used)
                return false;
            cache.gpu->release(device);
            return true;
        });
        std::erase_if(state.base_color_textures, [&](auto& cache) {
            if (cache.used)
                return false;
            device.destroy(cache.gpu);
            return true;
        });
        render_dirty_ = false;
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
    }

    void SceneView3D::release_render_resources() {
        if (!render_state_ || !render_state_->device)
            return;
        RenderState& state = *render_state_;
        for (auto& cache : state.point_clouds) {
            if (cache.gpu)
                cache.gpu->release(*state.device);
        }
        state.point_clouds.clear();
        for (auto& cache : state.base_color_textures) {
            if (cache.gpu)
                state.device->destroy(cache.gpu);
        }
        state.base_color_textures.clear();
        state.textured_mesh_vertex = {};
        state.textured_mesh_fragment = {};
        state.point_renderer.release(*state.device);
        if (state.color)
            state.device->destroy(state.color);
        if (state.depth)
            state.device->destroy(state.depth);
        state.device->invalidate_render_target_cache();
        state.color = {};
        state.depth = {};
        state.device = nullptr;
        render_dirty_ = true;
    }

} // namespace termin::gui_native
