#include "guard_main.h"

#include <memory>
#include <span>
#include <utility>

#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>

namespace {

class PipelineCacheStatsDevice final : public tgfx::IRenderDevice {
public:
    tgfx::BackendType backend_type() const override {
        return tgfx::BackendType::OpenGL;
    }

    tgfx::BackendCapabilities capabilities() const override {
        return {};
    }

    void wait_idle() override {}

    tgfx::BufferHandle create_buffer(const tgfx::BufferDesc&) override {
        return {};
    }

    tgfx::TextureHandle create_texture(const tgfx::TextureDesc&) override {
        return {};
    }

    tgfx::SamplerHandle create_sampler(const tgfx::SamplerDesc&) override {
        return {};
    }

    tgfx::ShaderHandle create_shader(const tgfx::ShaderDesc&) override {
        return {};
    }

    tgfx::PipelineHandle create_pipeline(const tgfx::PipelineDesc&) override {
        return tgfx::PipelineHandle{next_pipeline_id_++};
    }

    tgfx::ResourceSetHandle create_resource_set(const tgfx::ResourceSetDesc&) override {
        return {};
    }

    void destroy(tgfx::BufferHandle) override {}
    void destroy(tgfx::TextureHandle) override {}
    void destroy(tgfx::SamplerHandle) override {}
    void destroy(tgfx::ShaderHandle) override {}
    void destroy(tgfx::PipelineHandle) override {}
    void destroy(tgfx::ResourceSetHandle) override {}

    void upload_buffer(tgfx::BufferHandle, std::span<const uint8_t>, uint64_t = 0) override {}
    void upload_texture(tgfx::TextureHandle, std::span<const uint8_t>, uint32_t = 0) override {}
    void upload_texture_region(
        tgfx::TextureHandle,
        uint32_t,
        uint32_t,
        uint32_t,
        uint32_t,
        std::span<const uint8_t>,
        uint32_t = 0) override {}
    void read_buffer(tgfx::BufferHandle, std::span<uint8_t>, uint64_t = 0) override {}

    tgfx::TextureDesc texture_desc(tgfx::TextureHandle) const override {
        return {};
    }

    std::unique_ptr<tgfx::ICommandList> create_command_list(
        tgfx::QueueType = tgfx::QueueType::Graphics) override {
        return nullptr;
    }

    void submit(tgfx::ICommandList&) override {}
    void present() override {}

private:
    uint32_t next_pipeline_id_ = 1;
};

tgfx::VertexLayoutDesc make_layout(uint32_t stride, const char* semantic) {
    tgfx::VertexBufferLayout layout;
    layout.stride = stride;
    layout.attributes.push_back({
        0,
        tgfx::VertexFormat::Float3,
        0,
        semantic,
    });
    return tgfx::make_vertex_layout_desc(layout);
}

} // namespace

TEST_CASE("PipelineCache exposes backend-neutral hit miss and layout stats") {
    PipelineCacheStatsDevice device;
    tgfx::PipelineCache cache(device);

    tgfx::PipelineCacheKey key;
    key.vertex_shader = tgfx::ShaderHandle{1};
    key.fragment_shader = tgfx::ShaderHandle{2};
    key.vertex_layouts = {make_layout(12, "position")};
    key.vertex_layouts_hash = 0x1111;

    tgfx::PipelineHandle first = cache.get(key);
    CHECK(first.id == 1u);

    tgfx::PipelineCacheStats stats = cache.stats();
    CHECK(stats.hit_count == 0u);
    CHECK(stats.miss_count == 1u);
    CHECK(stats.create_pipeline_count == 1u);
    CHECK(stats.cached_pipeline_count == 1u);
    CHECK(stats.unique_vertex_layout_signature_count == 1u);
    REQUIRE(stats.vertex_layout_signature_hashes.size() == 1u);
    CHECK(stats.vertex_layout_signature_hashes[0] == 0x1111u);

    tgfx::PipelineHandle second = cache.get(key);
    CHECK(second == first);

    stats = cache.stats();
    CHECK(stats.hit_count == 1u);
    CHECK(stats.miss_count == 1u);
    CHECK(stats.create_pipeline_count == 1u);
    CHECK(stats.cached_pipeline_count == 1u);

    key.depth_stencil.depth_test = false;
    tgfx::PipelineHandle third = cache.get(key);
    CHECK(third.id == 2u);

    stats = cache.stats();
    CHECK(stats.hit_count == 1u);
    CHECK(stats.miss_count == 2u);
    CHECK(stats.create_pipeline_count == 2u);
    CHECK(stats.cached_pipeline_count == 2u);
    CHECK(stats.unique_vertex_layout_signature_count == 1u);

    key.vertex_layouts = {make_layout(24, "normal")};
    key.vertex_layouts_hash = 0x2222;
    tgfx::PipelineHandle fourth = cache.get(key);
    CHECK(fourth.id == 3u);

    stats = cache.stats();
    CHECK(stats.miss_count == 3u);
    CHECK(stats.create_pipeline_count == 3u);
    CHECK(stats.cached_pipeline_count == 3u);
    CHECK(stats.unique_vertex_layout_signature_count == 2u);
    REQUIRE(stats.vertex_layout_signature_hashes.size() == 2u);
    CHECK(stats.vertex_layout_signature_hashes[0] == 0x1111u);
    CHECK(stats.vertex_layout_signature_hashes[1] == 0x2222u);
}
