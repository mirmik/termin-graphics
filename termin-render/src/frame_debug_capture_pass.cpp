#include <termin/render/frame_debug_capture_pass.hpp>

#include <tcbase/tc_log.hpp>

#include <termin/render/execute_context.hpp>
#include <termin/render/frame_graph_debugger_core.hpp>

namespace termin {

FrameDebugCapturePass::FrameDebugCapturePass(const std::string& pass_name) {
    pass_name_set(pass_name);
    link_to_type_registry("FrameDebugCapturePass");
}

void FrameDebugCapturePass::set_source_resource(const std::string& resource) {
    source_resource = resource;
}

void FrameDebugCapturePass::set_source_type(const std::string& type) {
    source_type = type;
}

void FrameDebugCapturePass::set_paused(bool value) {
    paused = value;
}

void FrameDebugCapturePass::set_capture(FrameGraphCapture* capture) {
    capture_ = capture;
}

void FrameDebugCapturePass::set_depth_capture(FrameGraphCapture* capture) {
    depth_capture_ = capture;
}

std::set<const char*> FrameDebugCapturePass::compute_reads() const {
    if (source_resource.empty()) {
        return {};
    }
    return {source_resource.c_str()};
}

std::set<const char*> FrameDebugCapturePass::compute_writes() const {
    return {};
}

void FrameDebugCapturePass::execute(ExecuteContext& ctx) {
    if (paused || source_resource.empty()) {
        return;
    }
    if (!capture_) {
        tc::Log::warn("[FrameDebugCapturePass] capture target is not set");
        return;
    }
    if (!ctx.ctx2) {
        tc::Log::error("[FrameDebugCapturePass] ctx.ctx2 is null");
        return;
    }

    const bool explicit_depth_resource = source_is_depth();
    auto depth_it = ctx.tex2_depth_reads.find(source_resource);
    auto color_it = ctx.tex2_reads.find(source_resource);
    tgfx::TextureHandle depth_tex =
        depth_it != ctx.tex2_depth_reads.end() ? depth_it->second : tgfx::TextureHandle{};
    tgfx::TextureHandle color_tex =
        color_it != ctx.tex2_reads.end() ? color_it->second : tgfx::TextureHandle{};

    tgfx::TextureHandle src_tex{};
    if (explicit_depth_resource) {
        src_tex = depth_tex;
    } else {
        src_tex = color_tex;
        if (!src_tex) {
            src_tex = depth_tex;
        }
    }

    if (!src_tex) {
        tc::Log::warn(
            "[FrameDebugCapturePass] resource '%s' is not available in tex2 reads",
            source_resource.c_str());
        return;
    }

    capture_->capture_direct_via_ctx2(ctx.ctx2, src_tex, 0, 0);

    if (!depth_capture_) {
        return;
    }
    if (explicit_depth_resource || !depth_tex || depth_tex == src_tex) {
        depth_capture_->reset_capture();
        return;
    }
    depth_capture_->capture_direct_via_ctx2(ctx.ctx2, depth_tex, 0, 0);
}

bool FrameDebugCapturePass::source_is_depth() const {
    return source_type == "depth_texture" ||
           source_resource.ends_with(".depth") ||
           source_resource == "RT_DEPTH";
}

TC_DEFINE_FRAME_PASS_FACTORY_DERIVED(FrameDebugCapturePass, CxxFramePass);

void FrameDebugCapturePass::register_type() {
    register_frame_pass_FrameDebugCapturePass();
}

} // namespace termin
