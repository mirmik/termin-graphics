#include "tgfx2/render_runtime.hpp"

#include <stdexcept>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/pipeline_cache.hpp"
#include "tgfx2/render_context.hpp"

extern "C" {
#include "tgfx/tgfx2_interop.h"
}

namespace tgfx {

RenderRuntime::RenderRuntime(std::unique_ptr<IRenderDevice> device)
    : owned_device_(std::move(device)), device_(owned_device_.get()) {
    if (!device_) {
        throw std::invalid_argument("RenderRuntime: device is null");
    }
    publish_interop();
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

RenderContext2& RenderRuntime::context() {
    if (borrowed_ctx_) {
        return *borrowed_ctx_;
    }
    ensure_context_();
    return *owned_ctx_;
}

void RenderRuntime::publish_interop() {
    if (!device_) {
        return;
    }
    tgfx2_interop_set_device(device_);
    interop_published_by_us_ = true;
    tgfx2_gpu_ops_register();
}

void RenderRuntime::clear_interop_if_current() {
    if (interop_published_by_us_ && device_ && tgfx2_interop_get_device() == device_) {
        tgfx2_interop_set_device(nullptr);
    }
    interop_published_by_us_ = false;
}

void RenderRuntime::close() {
    clear_interop_if_current();
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
