// graphics_host.hpp - Shared tgfx2 device/cache/context ownership bundle.
#pragma once

#include <memory>

#include "tgfx2/enums.hpp"
#include "tgfx2/tgfx2_api.h"
#include "tgfx2/shader_artifact_resolver.hpp"

namespace tgfx {

class IRenderDevice;
class PipelineCache;
struct PipelineCacheStats;
class RenderContext2;

class TGFX2_TYPE_API GraphicsHost {
private:
    std::unique_ptr<IRenderDevice> owned_device_;
    IRenderDevice* device_ = nullptr;
    std::unique_ptr<PipelineCache> owned_cache_;
    std::unique_ptr<RenderContext2> owned_ctx_;
    bool interop_claimed_ = false;
    termin::ShaderArtifactResolver shader_artifacts_;

    explicit GraphicsHost(std::unique_ptr<IRenderDevice> device);
    void claim_application_domain();
    void release_application_domain();

public:
    ~GraphicsHost();

    GraphicsHost(const GraphicsHost&) = delete;
    GraphicsHost& operator=(const GraphicsHost&) = delete;

    // The application host owns the process graphics domain and publishes it
    // to legacy C interop. Isolated hosts are for tests/tools and never touch
    // that process-global compatibility registry.
    static std::unique_ptr<GraphicsHost> adopt_application_device(
        std::unique_ptr<IRenderDevice> device);
    static std::unique_ptr<GraphicsHost> create_application(BackendType backend);
    static std::unique_ptr<GraphicsHost> create_application_from_env();
    static std::unique_ptr<GraphicsHost> adopt_isolated_device(
        std::unique_ptr<IRenderDevice> device);
    static std::unique_ptr<GraphicsHost> create_isolated(BackendType backend);

    IRenderDevice& device();
    const IRenderDevice& device() const;

    PipelineCache& cache();
    PipelineCacheStats cache_stats() const;
    RenderContext2& context();

    bool owns_application_domain() const { return interop_claimed_; }
    bool is_closed() const { return device_ == nullptr; }
    void configure_shader_artifacts(const termin::ShaderArtifactResolver& resolver);
    const termin::ShaderArtifactResolver& shader_artifact_resolver() const;
    void close();

private:
    void ensure_context_();
};

} // namespace tgfx
