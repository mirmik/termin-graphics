#include <termin/render/execute_context.hpp>
#include <termin/render/frame_graph_capture.hpp>

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

} // namespace termin
