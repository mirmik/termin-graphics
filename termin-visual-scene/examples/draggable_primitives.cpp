#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

#include <termin/platform/backend_window.hpp>
#include <tgfx2/canvas2d_renderer.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>

#include "termin_visual_scene/interaction2d.hpp"
#include "termin_visual_scene/render_snapshot2d.hpp"

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

namespace {

using namespace termin::visual;

bool same(GraphicItemHandle a, GraphicItemHandle b) {
    return a.scene_id == b.scene_id &&
           a.index == b.index &&
           a.generation == b.generation;
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

class NoResources final
    : public SceneRenderResourceResolver2D,
      public tgfx::DrawResourceResolver2D {
public:
    std::optional<tgfx::FontHandle> resolve_font(
        const StableResourceRef2D&) override {
        return std::nullopt;
    }
    std::optional<tgfx::TextureHandle> resolve_image(
        const StableResourceRef2D&) override {
        return std::nullopt;
    }
    std::optional<ResolvedCustomBatch2D> resolve_custom_batch(
        const CustomBatchItem2D&) override {
        return std::nullopt;
    }
    tgfx::FontAtlas* resolve_font(tgfx::FontHandle) override {
        return nullptr;
    }
};

class DraggablePrimitiveScene {
public:
    DraggablePrimitiveScene() {
        rectangle_ = require_(scene_.create(RectItem2D{
            {0.0f, 0.0f, 150.0f, 100.0f},
            {{0.20f, 0.50f, 0.92f, 1.0f}},
            tgfx::StrokePaint{{0.08f, 0.15f, 0.28f, 1.0f}, 3.0f},
        }));
        ellipse_ = require_(scene_.create(EllipseItem2D{
            {0.0f, 0.0f, 120.0f, 120.0f},
            {{0.92f, 0.35f, 0.30f, 0.95f}},
            tgfx::StrokePaint{{0.35f, 0.08f, 0.06f, 1.0f}, 3.0f},
        }));
        path_ = require_(scene_.create(PathItem2D{
            diamond(130.0f, 100.0f),
            tgfx::FillPaint{{0.30f, 0.78f, 0.48f, 0.95f}},
            tgfx::StrokePaint{{0.06f, 0.28f, 0.12f, 1.0f}, 3.0f},
        }));

        place_(rectangle_, {110.0f, 110.0f}, 1);
        place_(ellipse_, {210.0f, 170.0f}, 2);
        place_(path_, {390.0f, 125.0f}, 3);
        base_colors_ = std::array<BaseColor, 3>{
            BaseColor{rectangle_, {0.20f, 0.50f, 0.92f, 1.0f}},
            BaseColor{ellipse_, {0.92f, 0.35f, 0.30f, 0.95f}},
            BaseColor{path_, {0.30f, 0.78f, 0.48f, 0.95f}},
        };
    }

    PointerDispatch2D pointer(const PointerEvent2D& event) {
        auto dispatch = interaction_.route(scene_, event);
        selection_.handle(scene_, dispatch);
        drag_.handle(scene_, dispatch);
        apply_feedback_();
        return dispatch;
    }

    std::optional<SceneRenderSnapshot2D> prepare(NoResources& resources) const {
        return scene_.prepare_render_snapshot(resources);
    }

    VisualScene2D& scene() { return scene_; }
    const VisualScene2D& scene() const { return scene_; }
    GraphicItemHandle rectangle() const { return rectangle_; }
    GraphicItemHandle ellipse() const { return ellipse_; }
    GraphicItemHandle path() const { return path_; }

private:
    struct BaseColor {
        GraphicItemHandle handle;
        tgfx::Color4f color;
    };

    static GraphicItemHandle require_(
        std::optional<GraphicItemHandle> value) {
        if (!value) throw std::runtime_error("failed to create example item");
        return *value;
    }

    void place_(
        GraphicItemHandle item,
        termin::Vec2f position,
        std::int64_t z) {
        auto state = scene_.snapshot(item)->state;
        state.local_transform = termin::Affine2f::translation(position);
        state.z_order = z;
        if (!scene_.set_state(item, state)) {
            throw std::runtime_error("failed to place example item");
        }
    }

    void apply_feedback_() {
        const auto hovered = interaction_.hovered(0);
        const auto selected = selection_.selection();
        for (const auto& entry : base_colors_) {
            auto item = scene_.snapshot(entry.handle);
            if (!item) continue;
            const bool is_selected = std::any_of(
                selected.begin(), selected.end(),
                [&](GraphicItemHandle value) {
                    return same(value, entry.handle);
                });
            const bool is_hovered = same(hovered, entry.handle);
            const tgfx::Color4f color = is_selected
                ? tgfx::Color4f{1.0f, 0.82f, 0.20f, 1.0f}
                : is_hovered
                    ? tgfx::Color4f{0.35f, 0.90f, 1.0f, 1.0f}
                    : entry.color;
            std::visit(
                [&](auto& payload) {
                    using T = std::decay_t<decltype(payload)>;
                    if constexpr (
                        std::is_same_v<T, RectItem2D> ||
                        std::is_same_v<T, EllipseItem2D>) {
                        payload.fill.color = color;
                    } else if constexpr (std::is_same_v<T, PathItem2D>) {
                        if (payload.fill) payload.fill->color = color;
                    }
                },
                item->payload);
            if (!scene_.set_payload(entry.handle, std::move(item->payload))) {
                throw std::runtime_error(
                    "failed to update example item feedback");
            }
        }
    }

    VisualScene2D scene_;
    SceneInteraction2D interaction_;
    SelectionController2D selection_;
    DragController2D drag_;
    GraphicItemHandle rectangle_ = tc_graphic_item_handle_invalid();
    GraphicItemHandle ellipse_ = tc_graphic_item_handle_invalid();
    GraphicItemHandle path_ = tc_graphic_item_handle_invalid();
    std::array<BaseColor, 3> base_colors_{};
};

termin::Vec2f world_center(
    const VisualScene2D& scene,
    GraphicItemHandle item) {
    const auto snapshot = scene.snapshot(item);
    const auto bounds = snapshot->local_bounds.value();
    return snapshot->world_transform.transform_point({
        (bounds.x0 + bounds.x1) * 0.5f,
        (bounds.y0 + bounds.y1) * 0.5f,
    });
}

int headless_smoke() {
    DraggablePrimitiveScene example;
    NoResources resources;
    const auto prepared = example.prepare(resources);
    if (!prepared || prepared->items().size() != 3) return 2;

    bool rect = false;
    bool ellipse = false;
    bool path = false;
    for (const auto& command : prepared->draw_list().commands()) {
        rect = rect ||
               std::holds_alternative<tgfx::DrawRect2D>(command) ||
               std::holds_alternative<tgfx::DrawRoundedRect2D>(command);
        ellipse = ellipse || std::holds_alternative<tgfx::DrawEllipse2D>(command);
        path = path || std::holds_alternative<tgfx::DrawPath2D>(command);
    }
    if (!rect || !ellipse || !path) return 3;

    const std::array handles{
        example.rectangle(), example.ellipse(), example.path()};
    PointerId2D pointer = 1;
    for (const auto handle : handles) {
        const auto start = world_center(example.scene(), handle);
        const auto down = example.pointer({
            pointer, PointerEventKind2D::Down, start, 0});
        if (!same(down.target, handle) || !same(down.captured, handle)) return 4;
        const termin::Vec2f end{start.x + 12.0f, start.y + 7.0f};
        const auto move = example.pointer({
            pointer, PointerEventKind2D::Move, end, 0});
        if (!same(move.target, handle)) return 5;
        const auto moved = world_center(example.scene(), handle);
        if (std::abs(moved.x - end.x) > 1e-3f ||
            std::abs(moved.y - end.y) > 1e-3f) {
            return 6;
        }
        const auto up = example.pointer({
            pointer, PointerEventKind2D::Up, end, 0});
        if (!same(up.target, handle) ||
            !tc_graphic_item_handle_is_invalid(up.captured)) {
            return 7;
        }
        ++pointer;
    }
    return 0;
}

tgfx::TextureHandle create_target(
    tgfx::IRenderDevice& device,
    int width,
    int height) {
    tgfx::TextureDesc desc;
    desc.width = static_cast<std::uint32_t>(width);
    desc.height = static_cast<std::uint32_t>(height);
    desc.format = tgfx::PixelFormat::RGBA8_UNorm;
    desc.usage =
        tgfx::TextureUsage::Sampled |
        tgfx::TextureUsage::ColorAttachment |
        tgfx::TextureUsage::CopySrc |
        tgfx::TextureUsage::CopyDst;
    return device.create_texture(desc);
}

int windowed_example() {
    tc_shader_init();
    try {
        auto runtime = termin::create_native_windowed_graphics();
        auto window = runtime->create_window({
            "VisualScene2D draggable primitives", 800, 600});
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
                } else if (
                    event.type == termin::WindowEventType::PointerMoved ||
                    event.type == termin::WindowEventType::PointerButtonPressed ||
                    event.type == termin::WindowEventType::PointerButtonReleased ||
                    event.type == termin::WindowEventType::PointerCaptureLost) {
                    PointerEventKind2D kind = PointerEventKind2D::Move;
                    if (event.type == termin::WindowEventType::PointerButtonPressed) {
                        kind = PointerEventKind2D::Down;
                    } else if (
                        event.type == termin::WindowEventType::PointerButtonReleased) {
                        kind = PointerEventKind2D::Up;
                    } else if (
                        event.type == termin::WindowEventType::PointerCaptureLost) {
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
            if (width <= 0 || height <= 0) continue;
            if (!target || width != target_width || height != target_height) {
                if (target) device.destroy(target);
                target = create_target(device, width, height);
                if (!target) throw std::runtime_error("failed to create color target");
                target_width = width;
                target_height = height;
            }

            const auto prepared = example.prepare(resources);
            if (!prepared) throw std::runtime_error("scene render preparation failed");
            const float clear[] = {0.035f, 0.045f, 0.065f, 1.0f};
            context.begin_frame();
            context.begin_pass(target, {}, clear, 1.0f, false);
            canvas.begin(context, width, height);
            if (!canvas.execute(prepared->draw_list(), resources)) {
                throw std::runtime_error("DrawList2D execution failed");
            }
            canvas.end();
            context.end_pass();
            context.end_frame();
            window->present(target);
        }

        canvas.release_gpu();
        device.wait_idle();
        if (target) device.destroy(target);
        window->close();
        runtime->close();
        tc_shader_shutdown();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "VisualScene2D example failed: %s\n", error.what());
        tc_shader_shutdown();
        return 1;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--headless-smoke") {
        return headless_smoke();
    }
    return windowed_example();
}
