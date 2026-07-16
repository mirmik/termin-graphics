// render_runtime.hpp - Shared tgfx2 device/cache/context ownership bundle.
#pragma once

#include <memory>

#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

class IRenderDevice;
class PipelineCache;
struct PipelineCacheStats;
class RenderContext2;

class TGFX2_TYPE_API RenderRuntime {
private:
    std::unique_ptr<IRenderDevice> owned_device_;
    IRenderDevice* device_ = nullptr;
    std::unique_ptr<PipelineCache> owned_cache_;
    std::unique_ptr<RenderContext2> owned_ctx_;
    RenderContext2* borrowed_ctx_ = nullptr;
    bool interop_claimed_ = false;

public:
    explicit RenderRuntime(std::unique_ptr<IRenderDevice> device);
    explicit RenderRuntime(IRenderDevice& borrowed_device);
    RenderRuntime(IRenderDevice& borrowed_device, RenderContext2& borrowed_ctx);
    ~RenderRuntime();

    RenderRuntime(const RenderRuntime&) = delete;
    RenderRuntime& operator=(const RenderRuntime&) = delete;

    static std::unique_ptr<RenderRuntime> create(BackendType backend);
    static std::unique_ptr<RenderRuntime> create_from_env();

    IRenderDevice& device();
    const IRenderDevice& device() const;

    PipelineCache& cache();
    PipelineCacheStats cache_stats() const;
    RenderContext2& context();

    bool owns_device() const { return static_cast<bool>(owned_device_); }
    bool owns_context() const { return owned_ctx_ != nullptr; }

    void claim_interop();
    void release_interop();
    bool interop_claimed() const { return interop_claimed_; }
    void close();

private:
    void ensure_context_();
};

} // namespace tgfx
