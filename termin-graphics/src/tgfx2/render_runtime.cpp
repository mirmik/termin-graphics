#include "tgfx2/render_runtime.hpp"

#include <stdexcept>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/render_context.hpp"

extern "C" {
#include "tgfx/tgfx2_interop.h"
#include <tcbase/tc_log.h>
}

namespace tgfx {

RenderRuntime::RenderRuntime(std::unique_ptr<IRenderDevice> device)
    : owned_device_(std::move(device)), device_(owned_device_.get()) {
    if (!device_) {
        throw std::invalid_argument("RenderRuntime: device is null");
    }
}

RenderRuntime::RenderRuntime(IRenderDevice& borrowed_device)
    : device_(&borrowed_device) {
}

RenderRuntime::RenderRuntime(IRenderDevice& borrowed_device,
                             RenderContext2& borrowed_ctx)
    : device_(&borrowed_device), borrowed_ctx_(&borrowed_ctx) {
}

RenderRuntime::~RenderRuntime() {
    close();
}

std::unique_ptr<RenderRuntime> RenderRuntime::create(BackendType backend) {
    return std::make_unique<RenderRuntime>(create_device(backend));
}

std::unique_ptr<RenderRuntime> RenderRuntime::create_from_env() {
    return create(default_backend_from_env());
}

IRenderDevice& RenderRuntime::device() {
    if (!device_) {
        throw std::runtime_error("RenderRuntime: device is unavailable");
    }
    return *device_;
}

const IRenderDevice& RenderRuntime::device() const {
    if (!device_) {
        throw std::runtime_error("RenderRuntime: device is unavailable");
    }
    return *device_;
}

PipelineCache& RenderRuntime::cache() {
    if (borrowed_ctx_ && !owned_cache_) {
        throw std::runtime_error(
            "RenderRuntime: cache is unavailable for a borrowed RenderContext2");
    }
    ensure_context_();
    return *owned_cache_;
}

PipelineCacheStats RenderRuntime::cache_stats() const {
    if (!owned_cache_) {
        return {};
    }
    return owned_cache_->stats();
}

RenderContext2& RenderRuntime::context() {
    if (borrowed_ctx_) {
        return *borrowed_ctx_;
    }
    ensure_context_();
    return *owned_ctx_;
}

void RenderRuntime::claim_interop() {
    if (!device_) {
        throw std::runtime_error("RenderRuntime: cannot claim interop without a device");
    }
    if (interop_claimed_) {
        return;
    }
    if (!tgfx2_interop_claim_device(device_, this)) {
        throw std::runtime_error(
            "RenderRuntime: another application graphics device is already installed");
    }
    interop_claimed_ = true;
}

void RenderRuntime::release_interop() {
    if (!interop_claimed_) {
        return;
    }
    if (!device_ || !tgfx2_interop_release_device(device_, this)) {
        tc_log_error("RenderRuntime: failed to release owned interop device");
    }
    interop_claimed_ = false;
}

void RenderRuntime::close() {
    release_interop();
    borrowed_ctx_ = nullptr;
    owned_ctx_.reset();
    owned_cache_.reset();
    owned_device_.reset();
    device_ = nullptr;
}

void RenderRuntime::ensure_context_() {
    if (borrowed_ctx_) {
        return;
    }
    if (!device_) {
        throw std::runtime_error("RenderRuntime: cannot create context without device");
    }
    if (!owned_cache_) {
        owned_cache_ = std::make_unique<PipelineCache>(*device_);
    }
    if (!owned_ctx_) {
        owned_ctx_ = std::make_unique<RenderContext2>(*device_, *owned_cache_);
    }
}

} // namespace tgfx
