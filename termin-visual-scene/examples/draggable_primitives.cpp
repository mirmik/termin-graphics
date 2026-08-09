#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <termin/geom/color.hpp>

#include <termin/platform/backend_window.hpp>
#include <tgfx2/canvas2d_renderer.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/device_factory.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/shader_artifact_resolver.hpp>
#include <tgfx2/standalone_shader_runtime.hpp>

#include "termin_visual_scene/builtin_items2d.hpp"
#include "termin_visual_scene/interaction2d.hpp"
#include "termin_visual_scene/scene_render2d.hpp"

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace {

    using namespace termin::visual;

    bool same(GraphicItemHandle a, GraphicItemHandle b) {
        return a.scene_id == b.scene_id && a.index == b.index && a.generation == b.generation;
    }

    tgfx::Path2f diamond(float width, float height) {
        tgfx::Path2f path;
        path.move_to({width * 0.5f, 0.0f});
        path.line_to({width, height * 0.5f});
        path.line_to({width * 0.5f, height});
        path.line_to({0.0f, height * 0.5f});
        path.close();
        return path;
    }

    class NoResources final : public SceneRenderResourceResolver2D, public tgfx::DrawResourceResolver2D {
    public:
        std::optional<tgfx::FontHandle> resolve_font(std::string_view) override {
            return std::nullopt;
        }
        std::optional<tgfx::TextureHandle> resolve_image(std::string_view) override {
            return std::nullopt;
        }
        std::optional<ResolvedCustomBatch2D> resolve_custom_batch(std::string_view, termin::Bounds2f) override {
            return std::nullopt;
        }
        tgfx::FontAtlas* resolve_font(tgfx::FontHandle) override {
            return nullptr;
        }
    };

    class DraggablePrimitiveScene {
    public:
        DraggablePrimitiveScene()
            : scene_(tc_visual_scene_create()) {
            auto rectangle = std::make_unique<RectItem2D>(termin::Rect2f{0.0f, 0.0f, 150.0f, 100.0f},
                                                          tgfx::FillPaint{{0.20f, 0.50f, 0.92f, 1.0f}},
                                                          tgfx::StrokePaint{{0.08f, 0.15f, 0.28f, 1.0f}, 3.0f});
            rectangle_item_ = rectangle.get();
            rectangle_ = require_(scene_.adopt(std::move(rectangle)));

            auto ellipse = std::make_unique<EllipseItem2D>(termin::Rect2f{0.0f, 0.0f, 120.0f, 120.0f},
                                                           tgfx::FillPaint{{0.92f, 0.35f, 0.30f, 0.95f}},
                                                           tgfx::StrokePaint{{0.35f, 0.08f, 0.06f, 1.0f}, 3.0f});
            ellipse_item_ = ellipse.get();
            ellipse_ = require_(scene_.adopt(std::move(ellipse)));

            auto path = std::make_unique<PathItem2D>(diamond(130.0f, 100.0f),
                                                     tgfx::FillPaint{{0.30f, 0.78f, 0.48f, 0.95f}},
                                                     tgfx::StrokePaint{{0.06f, 0.28f, 0.12f, 1.0f}, 3.0f});
            path_item_ = path.get();
            path_ = require_(scene_.adopt(std::move(path)));

            place_(rectangle_, {110.0f, 110.0f}, 1);
            place_(ellipse_, {210.0f, 170.0f}, 2);
            place_(path_, {390.0f, 125.0f}, 3);
            base_colors_ = std::array<BaseColor, 3>{
                BaseColor{rectangle_, {0.20f, 0.50f, 0.92f, 1.0f}},
                BaseColor{ellipse_, {0.92f, 0.35f, 0.30f, 0.95f}},
                BaseColor{path_, {0.30f, 0.78f, 0.48f, 0.95f}},
            };
        }
        ~DraggablePrimitiveScene() {
            tc_visual_scene_destroy(scene_.handle());
        }

        PointerDispatch2D pointer(const PointerEvent2D& event) {
            auto dispatch = interaction_.route(scene_, event);
            selection_.handle(scene_, dispatch);
            drag_.handle(scene_, dispatch);
            apply_feedback_();
            return dispatch;
        }

        std::optional<tgfx::DrawList2D> render(NoResources& resources) const {
            tgfx::DrawList2DBuilder builder;
            if (!scene_.paint(builder, resources)) {
                return std::nullopt;
            }
            return builder.freeze();
        }

        TcVisualScene& scene() {
            return scene_;
        }
        const TcVisualScene& scene() const {
            return scene_;
        }
        GraphicItemHandle rectangle() const {
            return rectangle_;
        }
        GraphicItemHandle ellipse() const {
            return ellipse_;
        }
        GraphicItemHandle path() const {
            return path_;
        }

    private:
        struct BaseColor {
            GraphicItemHandle handle;
            termin::SrgbColor color;
        };

        static GraphicItemHandle require_(std::optional<GraphicItemHandle> value) {
            if (!value)
                throw std::runtime_error("failed to create example item");
            return *value;
        }

        void place_(GraphicItemHandle item, termin::Vec2f position, std::int64_t z) {
            tc_graphic_item* object = scene_.resolve(item);
            if (!object) {
                throw std::runtime_error("failed to place example item");
            }
            object->local_transform = termin::Affine2f::translation(position);
            object->z_order = z;
        }

        void apply_feedback_() {
            const auto hovered = interaction_.hovered(0);
            const auto selected = selection_.selection();
            for (const auto& entry : base_colors_) {
                const bool is_selected = std::any_of(selected.begin(), selected.end(), [&](GraphicItemHandle value) {
                    return same(value, entry.handle);
                });
                const bool is_hovered = same(hovered, entry.handle);
                const termin::SrgbColor color = is_selected  ? termin::SrgbColor{1.0f, 0.82f, 0.20f, 1.0f}
                                            : is_hovered ? termin::SrgbColor{0.35f, 0.90f, 1.0f, 1.0f}
                                                         : entry.color;
                const tgfx::FillPaint fill{termin::srgb_to_linear(color)};
                if (same(entry.handle, rectangle_)) {
                    rectangle_item_->set_fill(fill);
                } else if (same(entry.handle, ellipse_)) {
                    ellipse_item_->set_fill(fill);
                } else if (same(entry.handle, path_)) {
                    path_item_->set_fill(fill);
                }
            }
        }

        TcVisualScene scene_;
        SceneInteraction2D interaction_;
        SelectionController2D selection_;
        DragController2D drag_;
        GraphicItemHandle rectangle_ = tc_graphic_item_handle_invalid();
        GraphicItemHandle ellipse_ = tc_graphic_item_handle_invalid();
        GraphicItemHandle path_ = tc_graphic_item_handle_invalid();
        RectItem2D* rectangle_item_ = nullptr;
        EllipseItem2D* ellipse_item_ = nullptr;
        PathItem2D* path_item_ = nullptr;
        std::array<BaseColor, 3> base_colors_{};
    };

    termin::Vec2f world_center(const TcVisualScene& scene, GraphicItemHandle item) {
        const tc_graphic_item* object = scene.resolve(item);
        if (!object) {
            throw std::runtime_error("example item is stale");
        }
        const auto bounds = scene.local_bounds(*object).value();
        return scene.world_transform(*object).transform_point({
            (bounds.x0 + bounds.x1) * 0.5f,
            (bounds.y0 + bounds.y1) * 0.5f,
        });
    }

    int headless_smoke() {
        DraggablePrimitiveScene example;
        NoResources resources;
        const auto rendered = example.render(resources);
        if (!rendered || example.scene().size() != 3)
            return 2;

        bool rect = false;
        bool ellipse = false;
        bool path = false;
        for (const auto& command : rendered->commands()) {
            rect = rect || std::holds_alternative<tgfx::DrawRect2D>(command) ||
                   std::holds_alternative<tgfx::DrawRoundedRect2D>(command);
            ellipse = ellipse || std::holds_alternative<tgfx::DrawEllipse2D>(command);
            path = path || std::holds_alternative<tgfx::DrawPath2D>(command);
        }
        if (!rect || !ellipse || !path)
            return 3;

        const std::array handles{example.rectangle(), example.ellipse(), example.path()};
        PointerId2D pointer = 1;
        for (const auto handle : handles) {
            const auto start = world_center(example.scene(), handle);
            const auto down = example.pointer({pointer, PointerEventKind2D::Down, start, 0});
            if (!same(down.target, handle) || !same(down.captured, handle))
                return 4;
            const termin::Vec2f end{start.x + 12.0f, start.y + 7.0f};
            const auto move = example.pointer({pointer, PointerEventKind2D::Move, end, 0});
            if (!same(move.target, handle))
                return 5;
            const auto moved = world_center(example.scene(), handle);
            if (std::abs(moved.x - end.x) > 1e-3f || std::abs(moved.y - end.y) > 1e-3f) {
                return 6;
            }
            const auto up = example.pointer({pointer, PointerEventKind2D::Up, end, 0});
            if (!same(up.target, handle) || !tc_graphic_item_handle_is_invalid(up.captured)) {
                return 7;
            }
            ++pointer;
        }
        return 0;
    }

    tgfx::TextureHandle create_target(tgfx::IRenderDevice& device, int width, int height);

    int shader_smoke(bool force_missing_artifacts = false) {
        tc_shader_init();
        try {
            tgfx::BackendType backend = tgfx::BackendType::Null;
            if (tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
                backend = tgfx::BackendType::Vulkan;
            } else if (tgfx::backend_is_compiled(tgfx::BackendType::D3D11)) {
                backend = tgfx::BackendType::D3D11;
            } else {
                std::fprintf(stderr, "VisualScene2D shader smoke skipped: no headless backend\n");
                tc_shader_shutdown();
                return 77;
            }

            auto host = tgfx::GraphicsHost::create_isolated(backend);
            if (!tgfx::configure_default_standalone_shader_runtime(*host, "visual-scene-draggable-smoke")) {
                throw std::runtime_error("standalone shader runtime configuration failed");
            }
            if (force_missing_artifacts) {
                host->configure_shader_artifacts(termin::ShaderArtifactResolver(
                    "/__termin_intentionally_missing_shader_artifacts__", "", "", false, false));
            }

            constexpr int width = 640;
            constexpr int height = 480;
            auto target = create_target(host->device(), width, height);
            if (!target) {
                throw std::runtime_error("failed to create shader smoke target");
            }

            DraggablePrimitiveScene example;
            NoResources resources;
            const auto rendered = example.render(resources);
            if (!rendered) {
                throw std::runtime_error("shader smoke scene rendering failed");
            }

            tgfx::Canvas2DRenderer canvas;
            const termin::LinearColor clear{0.035f, 0.045f, 0.065f, 1.0f};
            auto& context = host->context();
            context.begin_frame();
            context.begin_pass(target, {}, &clear, 1.0f, false);
            canvas.begin(context, width, height);
            if (!canvas.execute(*rendered, resources)) {
                throw std::runtime_error("shader smoke DrawList2D execution failed");
            }
            canvas.end();
            context.end_pass();
            context.end_frame();
            host->device().wait_idle();

            canvas.release_gpu();
            host->device().destroy(target);
            host->close();
            tc_shader_shutdown();
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "VisualScene2D shader smoke failed: %s\n", error.what());
            tc_shader_shutdown();
            return 1;
        }
    }

    tgfx::TextureHandle create_target(tgfx::IRenderDevice& device, int width, int height) {
        tgfx::TextureDesc desc;
        desc.width = static_cast<std::uint32_t>(width);
        desc.height = static_cast<std::uint32_t>(height);
        desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc |
                     tgfx::TextureUsage::CopyDst;
        return device.create_texture(desc);
    }

    int windowed_example() {
        tc_shader_init();
        try {
            auto runtime = termin::create_native_windowed_graphics();
            if (!tgfx::configure_default_standalone_shader_runtime(runtime->graphics(), "visual-scene-draggable")) {
                throw std::runtime_error("standalone shader runtime configuration failed");
            }
            auto window = runtime->create_window({"TcVisualScene draggable primitives", 800, 600});
            auto& host = runtime->graphics();
            auto& device = host.device();
            auto& context = host.context();
            tgfx::Canvas2DRenderer canvas;
            DraggablePrimitiveScene example;
            NoResources resources;
            tgfx::TextureHandle target{};
            int target_width = 0;
            int target_height = 0;

            while (!window->should_close()) {
                termin::WindowEvent event;
                while (window->poll_event(event)) {
                    if (event.type == termin::WindowEventType::CloseRequested) {
                        window->set_should_close(true);
                    } else if (event.type == termin::WindowEventType::PointerMoved ||
                               event.type == termin::WindowEventType::PointerButtonPressed ||
                               event.type == termin::WindowEventType::PointerButtonReleased ||
                               event.type == termin::WindowEventType::PointerCaptureLost) {
                        PointerEventKind2D kind = PointerEventKind2D::Move;
                        if (event.type == termin::WindowEventType::PointerButtonPressed) {
                            kind = PointerEventKind2D::Down;
                        } else if (event.type == termin::WindowEventType::PointerButtonReleased) {
                            kind = PointerEventKind2D::Up;
                        } else if (event.type == termin::WindowEventType::PointerCaptureLost) {
                            kind = PointerEventKind2D::Cancel;
                        }
                        example.pointer({
                            0,
                            kind,
                            {
                                event.pointer.framebuffer_position.x,
                                event.pointer.framebuffer_position.y,
                            },
                            static_cast<std::uint32_t>(event.pointer.button),
                        });
                    }
                }

                const auto [width, height] = window->framebuffer_size();
                if (width <= 0 || height <= 0)
                    continue;
                if (!target || width != target_width || height != target_height) {
                    if (target)
                        device.destroy(target);
                    target = create_target(device, width, height);
                    if (!target)
                        throw std::runtime_error("failed to create color target");
                    target_width = width;
                    target_height = height;
                }

                const auto rendered = example.render(resources);
                if (!rendered)
                    throw std::runtime_error("scene rendering failed");
                const termin::LinearColor clear{0.035f, 0.045f, 0.065f, 1.0f};
                context.begin_frame();
                context.begin_pass(target, {}, &clear, 1.0f, false);
                canvas.begin(context, width, height);
                if (!canvas.execute(*rendered, resources)) {
                    throw std::runtime_error("DrawList2D execution failed");
                }
                canvas.end();
                context.end_pass();
                context.end_frame();
                window->present(target);
            }

            canvas.release_gpu();
            device.wait_idle();
            if (target)
                device.destroy(target);
            window->close();
            runtime->close();
            tc_shader_shutdown();
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "TcVisualScene example failed: %s\n", error.what());
            tc_shader_shutdown();
            return 1;
        }
    }

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--headless-smoke") {
        return headless_smoke();
    }
    if (argc > 1 && std::string_view(argv[1]) == "--shader-smoke") {
        return shader_smoke();
    }
    if (argc > 1 && std::string_view(argv[1]) == "--shader-smoke-missing-artifacts") {
        return shader_smoke(true);
    }
    return windowed_example();
}
