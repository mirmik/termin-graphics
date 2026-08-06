#include "tcplot/retained_scene_renderer2d.h"

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <tcbase/tc_log.hpp>
#include <tgfx2/canvas2d_renderer.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>
#include <termin_visual_scene/scene2d.hpp>
#include <termin_visual_scene/scene_render2d.hpp>

#include "tcplot/gpu_host.hpp"

namespace {

constexpr std::string_view kDefaultFontUri = "ui://default-font";

class RetainedSceneResources final
    : public termin::visual::SceneRenderResourceResolver2D,
      public tgfx::DrawResourceResolver2D {
public:
    explicit RetainedSceneResources(tgfx::FontAtlas& font)
        : font_(&font) {}

    std::optional<tgfx::FontHandle> resolve_font(
        std::string_view uri) override {
        if (uri == kDefaultFontUri) {
            return tgfx::FontHandle{1};
        }
        tc::Log::error(
            "RetainedSceneRenderer2D: unresolved font resource '%s'",
            std::string(uri).c_str());
        return std::nullopt;
    }

    std::optional<tgfx::TextureHandle> resolve_image(
        std::string_view uri) override {
        tc::Log::error(
            "RetainedSceneRenderer2D: unresolved image resource '%s'",
            std::string(uri).c_str());
        return std::nullopt;
    }

    std::optional<termin::visual::ResolvedCustomBatch2D>
    resolve_custom_batch(
        std::string_view key,
        termin::Bounds2f) override {
        tc::Log::error(
            "RetainedSceneRenderer2D: unresolved custom batch '%s'",
            std::string(key).c_str());
        return std::nullopt;
    }

    tgfx::FontAtlas* resolve_font(tgfx::FontHandle handle) override {
        return handle.id == 1 ? font_ : nullptr;
    }

private:
    tgfx::FontAtlas* font_;
};

class RetainedSceneRenderer2D {
public:
    RetainedSceneRenderer2D(
        tcplot::GpuHost& host,
        tc_visual_scene_handle scene)
        : host_(&host),
          scene_(scene),
          canvas_(&host.font()),
          resources_(host.font()) {
        if (!scene_.valid()) {
            throw std::invalid_argument(
                "RetainedSceneRenderer2D requires a live visual scene");
        }
    }

    ~RetainedSceneRenderer2D() {
        release_gpu();
    }

    void set_clear_color(float r, float g, float b, float a) {
        clear_color_[0] = r;
        clear_color_[1] = g;
        clear_color_[2] = b;
        clear_color_[3] = a;
    }

    uint32_t render(int width, int height) {
        if (width <= 0 || height <= 0) {
            tc::Log::error(
                "RetainedSceneRenderer2D: invalid target size %dx%d",
                width,
                height);
            return 0;
        }
        if (!scene_.valid()) {
            tc::Log::error(
                "RetainedSceneRenderer2D: borrowed visual scene is stale");
            return 0;
        }

        tgfx::DrawList2DBuilder builder;
        if (!scene_.paint(builder, resources_)) {
            tc::Log::error(
                "RetainedSceneRenderer2D: failed to paint visual scene");
            return 0;
        }
        tgfx::DrawList2D draw_list = builder.freeze();
        ensure_offscreen(width, height);

        tgfx::RenderContext2& ctx = host_->ctx();
        ctx.begin_frame();
        ctx.begin_pass(
            offscreen_color_,
            tgfx::TextureHandle{},
            clear_color_,
            1.0f,
            false);
        canvas_.begin(ctx, width, height);
        const bool executed = canvas_.execute(draw_list, resources_);
        canvas_.end();
        ctx.end_pass();
        ctx.end_frame();

        if (!executed) {
            tc::Log::error(
                "RetainedSceneRenderer2D: failed to execute visual scene draw list");
            return 0;
        }
        return offscreen_color_.id;
    }

    void release_gpu() {
        canvas_.release_gpu();
        if (host_ && offscreen_color_.id != 0) {
            host_->device().destroy(offscreen_color_);
        }
        offscreen_color_ = {};
        offscreen_width_ = 0;
        offscreen_height_ = 0;
    }

private:
    void ensure_offscreen(int width, int height) {
        if (offscreen_color_.id != 0 &&
            offscreen_width_ == width &&
            offscreen_height_ == height) {
            return;
        }
        if (offscreen_color_.id != 0) {
            host_->device().destroy(offscreen_color_);
        }

        tgfx::TextureDesc desc;
        desc.width = static_cast<uint32_t>(width);
        desc.height = static_cast<uint32_t>(height);
        desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        desc.usage = tgfx::TextureUsage::Sampled |
                     tgfx::TextureUsage::ColorAttachment |
                     tgfx::TextureUsage::CopySrc;
        offscreen_color_ = host_->device().create_texture(desc);
        if (offscreen_color_.id == 0) {
            throw std::runtime_error(
                "RetainedSceneRenderer2D failed to create offscreen texture");
        }
        offscreen_width_ = width;
        offscreen_height_ = height;
    }

    tcplot::GpuHost* host_;
    termin::visual::TcVisualScene scene_;
    tgfx::Canvas2DRenderer canvas_;
    RetainedSceneResources resources_;
    tgfx::TextureHandle offscreen_color_{};
    int offscreen_width_ = 0;
    int offscreen_height_ = 0;
    float clear_color_[4] = {0.08f, 0.09f, 0.11f, 1.0f};
};

template <typename Function, typename Result>
Result logged_call(const char* operation, Result failure, Function&& function) {
    try {
        return function();
    } catch (const std::exception& error) {
        tc::Log::error(
            "RetainedSceneRenderer2D: %s failed: %s",
            operation,
            error.what());
    } catch (...) {
        tc::Log::error(
            "RetainedSceneRenderer2D: %s failed with an unknown exception",
            operation);
    }
    return failure;
}

}  // namespace

struct tc_retained_scene_renderer2d {
    RetainedSceneRenderer2D value;

    tc_retained_scene_renderer2d(
        tcplot::GpuHost& host,
        tc_visual_scene_handle scene)
        : value(host, scene) {}
};

extern "C" {

tc_retained_scene_renderer2d* tc_retained_scene_renderer2d_create(
    void* gpu_host,
    tc_visual_scene_handle scene) {
    if (!gpu_host) {
        tc::Log::error(
            "RetainedSceneRenderer2D: create requires a live GpuHost");
        return nullptr;
    }
    return logged_call(
        "create",
        static_cast<tc_retained_scene_renderer2d*>(nullptr),
        [&] {
            return new tc_retained_scene_renderer2d(
                *static_cast<tcplot::GpuHost*>(gpu_host),
                scene);
        });
}

void tc_retained_scene_renderer2d_destroy(
    tc_retained_scene_renderer2d* renderer) {
    if (!renderer) return;
    try {
        delete renderer;
    } catch (const std::exception& error) {
        tc::Log::error(
            "RetainedSceneRenderer2D: destroy failed: %s",
            error.what());
    } catch (...) {
        tc::Log::error(
            "RetainedSceneRenderer2D: destroy failed with an unknown exception");
    }
}

void tc_retained_scene_renderer2d_set_clear_color(
    tc_retained_scene_renderer2d* renderer,
    float r,
    float g,
    float b,
    float a) {
    if (!renderer) {
        tc::Log::error(
            "RetainedSceneRenderer2D: set_clear_color called with null renderer");
        return;
    }
    renderer->value.set_clear_color(r, g, b, a);
}

uint32_t tc_retained_scene_renderer2d_render(
    tc_retained_scene_renderer2d* renderer,
    int width,
    int height) {
    if (!renderer) {
        tc::Log::error(
            "RetainedSceneRenderer2D: render called with null renderer");
        return 0;
    }
    return logged_call(
        "render",
        uint32_t{0},
        [&] { return renderer->value.render(width, height); });
}

void tc_retained_scene_renderer2d_release_gpu(
    tc_retained_scene_renderer2d* renderer) {
    if (!renderer) return;
    logged_call(
        "release_gpu",
        false,
        [&] {
            renderer->value.release_gpu();
            return true;
        });
}

}  // extern "C"
