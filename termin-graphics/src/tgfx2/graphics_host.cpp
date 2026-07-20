#include "tgfx2/graphics_host.hpp"

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

GraphicsHost::GraphicsHost(std::unique_ptr<IRenderDevice> device)
    : owned_device_(std::move(device)), device_(owned_device_.get()) {
    if (!device_) {
        throw std::invalid_argument("GraphicsHost: device is null");
    }
}

GraphicsHost::~GraphicsHost() {
    close();
}

std::unique_ptr<GraphicsHost> GraphicsHost::adopt_application_device(
    std::unique_ptr<IRenderDevice> device) {
    auto host = std::unique_ptr<GraphicsHost>(new GraphicsHost(std::move(device)));
    host->claim_application_domain();
    return host;
}

std::unique_ptr<GraphicsHost> GraphicsHost::create_application(BackendType backend) {
    return adopt_application_device(create_device(backend));
}

std::unique_ptr<GraphicsHost> GraphicsHost::create_application_from_env() {
    return create_application(default_backend_from_env());
}

std::unique_ptr<GraphicsHost> GraphicsHost::adopt_isolated_device(
    std::unique_ptr<IRenderDevice> device) {
    return std::unique_ptr<GraphicsHost>(new GraphicsHost(std::move(device)));
}

std::unique_ptr<GraphicsHost> GraphicsHost::create_isolated(BackendType backend) {
    return adopt_isolated_device(create_device(backend));
}

IRenderDevice& GraphicsHost::device() {
    if (!device_) {
        throw std::runtime_error("GraphicsHost: device is unavailable");
    }
    return *device_;
}

const IRenderDevice& GraphicsHost::device() const {
    if (!device_) {
        throw std::runtime_error("GraphicsHost: device is unavailable");
    }
    return *device_;
}

PipelineCache& GraphicsHost::cache() {
    ensure_context_();
    return *owned_cache_;
}

PipelineCacheStats GraphicsHost::cache_stats() const {
    if (!owned_cache_) {
        return {};
    }
    return owned_cache_->stats();
}

RenderContext2& GraphicsHost::context() {
    ensure_context_();
    return *owned_ctx_;
}

void GraphicsHost::claim_application_domain() {
    if (!device_) {
        throw std::runtime_error("GraphicsHost: cannot claim interop without a device");
    }
    if (interop_claimed_) {
        return;
    }
    if (!tgfx2_interop_claim_device(device_, this)) {
        throw std::runtime_error(
            "GraphicsHost: another application graphics device is already installed");
    }
    interop_claimed_ = true;
}

void GraphicsHost::release_application_domain() {
    if (!interop_claimed_) {
        return;
    }
    if (!device_ || !tgfx2_interop_release_device(device_, this)) {
        tc_log_error("GraphicsHost: failed to release owned interop device");
    }
    interop_claimed_ = false;
}

void GraphicsHost::configure_shader_artifacts(
    const termin::ShaderArtifactResolver& resolver
) {
    if (!device_) {
        throw std::runtime_error(
            "GraphicsHost: cannot configure shader artifacts without a device");
    }
    shader_artifacts_ = resolver;
    device_->configure_shader_artifacts(shader_artifacts_);
}

const termin::ShaderArtifactResolver& GraphicsHost::shader_artifact_resolver() const {
    if (!device_) {
        throw std::runtime_error(
            "GraphicsHost: shader artifact resolver is unavailable without a device");
    }
    return device_->shader_artifact_resolver();
}

void GraphicsHost::close() {
    release_application_domain();
    owned_ctx_.reset();
    owned_cache_.reset();
    owned_device_.reset();
    device_ = nullptr;
}

void GraphicsHost::ensure_context_() {
    if (!device_) {
        throw std::runtime_error("GraphicsHost: cannot create context without device");
    }
    if (!owned_cache_) {
        owned_cache_ = std::make_unique<PipelineCache>(*device_);
    }
    if (!owned_ctx_) {
        owned_ctx_ = std::make_unique<RenderContext2>(*device_, *owned_cache_);
    }
}

} // namespace tgfx
