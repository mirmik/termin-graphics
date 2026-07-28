#include <termin/render/execute_context.hpp>
#include <termin/render/frame_graph_capture.hpp>

#include <cstring>

#include <tcbase/tc_log.hpp>

namespace termin {

const std::string* ExecuteContext::requested_internal_symbol() const {
    for (const FrameGraphCaptureRequest* request : debug_internal_capture_requests) {
        if (request && request->kind == FrameGraphCaptureRequestKind::InternalSymbol
                && !request->paused && !request->internal_symbol.empty()) {
            return &request->internal_symbol;
        }
    }
    return nullptr;
}

bool ExecuteContext::should_capture_internal(const char* symbol) const {
    if (!symbol) return false;
    for (const FrameGraphCaptureRequest* request : debug_internal_capture_requests) {
        if (request && request->kind == FrameGraphCaptureRequestKind::InternalSymbol
                && !request->paused && request->capture
                && request->internal_symbol == symbol) {
            return true;
        }
    }
    return false;
}

bool ExecuteContext::capture_internal(
    const char* symbol,
    tgfx::TextureHandle texture,
    int width,
    int height,
    tgfx::PixelFormat format
) {
    bool captured = false;
    if (!symbol || !texture) return false;
    for (FrameGraphCaptureRequest* request : debug_internal_capture_requests) {
        if (!request || request->kind != FrameGraphCaptureRequestKind::InternalSymbol
                || request->paused || !request->capture
                || request->internal_symbol != symbol) {
            continue;
        }
        request->capture->reset_capture();
        request->capture->capture_direct_via_ctx2(ctx2, texture, width, height, format);
        request->status = request->capture->has_capture()
            ? FrameGraphCaptureRequestStatus::Captured
            : FrameGraphCaptureRequestStatus::ResourceUnavailable;
        captured = request->status == FrameGraphCaptureRequestStatus::Captured || captured;
    }
    return captured;
}

bool ExecuteContext::build_render_pass(
    std::span<const FrameGraphColorAttachment> colors,
    const FrameGraphDepthAttachment* depth,
    tgfx::RenderPassDesc& out_pass
) const {
    out_pass = {};
    if (colors.size() > tgfx::TGFX2_MAX_COLOR_ATTACHMENTS) {
        tc::Log::error(
            "ExecuteContext::build_render_pass: %zu color attachments exceed engine limit %u",
            colors.size(),
            tgfx::TGFX2_MAX_COLOR_ATTACHMENTS);
        return false;
    }

    out_pass.colors.reserve(colors.size());
    for (size_t index = 0; index < colors.size(); ++index) {
        const FrameGraphColorAttachment& source = colors[index];
        if (!source.resource_name || source.resource_name[0] == '\0') {
            tc::Log::error(
                "ExecuteContext::build_render_pass: color attachment %zu has no resource name",
                index);
            out_pass = {};
            return false;
        }
        const auto resource = tex2_writes.find(source.resource_name);
        if (resource == tex2_writes.end() || !resource->second) {
            tc::Log::error(
                "ExecuteContext::build_render_pass: color output '%s' is unavailable",
                source.resource_name);
            out_pass = {};
            return false;
        }
        for (const tgfx::ColorAttachmentDesc& existing : out_pass.colors) {
            if (existing.texture == resource->second) {
                tc::Log::error(
                    "ExecuteContext::build_render_pass: color output '%s' aliases an earlier MRT slot",
                    source.resource_name);
                out_pass = {};
                return false;
            }
        }

        tgfx::ColorAttachmentDesc target;
        target.texture = resource->second;
        target.load = source.load;
        target.store = source.store;
        std::memcpy(
            target.clear_color,
            source.clear_color,
            sizeof(target.clear_color));
        out_pass.colors.push_back(target);
    }

    if (depth) {
        if (!depth->resource_name || depth->resource_name[0] == '\0') {
            tc::Log::error(
                "ExecuteContext::build_render_pass: depth attachment has no resource name");
            out_pass = {};
            return false;
        }
        const auto resource = tex2_depth_writes.find(depth->resource_name);
        if (resource == tex2_depth_writes.end() || !resource->second) {
            tc::Log::error(
                "ExecuteContext::build_render_pass: depth output '%s' is unavailable",
                depth->resource_name);
            out_pass = {};
            return false;
        }
        for (const tgfx::ColorAttachmentDesc& color : out_pass.colors) {
            if (color.texture == resource->second) {
                tc::Log::error(
                    "ExecuteContext::build_render_pass: depth output '%s' aliases a color MRT slot",
                    depth->resource_name);
                out_pass = {};
                return false;
            }
        }

        out_pass.has_depth = true;
        out_pass.depth.texture = resource->second;
        out_pass.depth.load = depth->load;
        out_pass.depth.store = depth->store;
        out_pass.depth.clear_depth = depth->clear_depth;
        out_pass.depth.clear_stencil = depth->clear_stencil;
    }

    if (out_pass.colors.empty() && !out_pass.has_depth) {
        tc::Log::error(
            "ExecuteContext::build_render_pass: pass has no attachments");
        return false;
    }
    return true;
}

} // namespace termin
