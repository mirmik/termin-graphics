#include "guard_main.h"

#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/texture_pool.hpp>

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
        ++create_texture_count;
        if (texture_failures_remaining > 0) {
            --texture_failures_remaining;
            return {};
        }
        return tgfx::TextureHandle{next_texture_id_++};
    }

    tgfx::SamplerHandle create_sampler(const tgfx::SamplerDesc&) override {
        return {};
    }

    tgfx::ShaderHandle create_shader(const tgfx::ShaderDesc&) override {
        return {};
    }

    tgfx::PipelineHandle create_pipeline(const tgfx::PipelineDesc&) override {
        ++create_pipeline_count;
        if (pipeline_failures_remaining > 0) {
            --pipeline_failures_remaining;
            return {};
        }
        return tgfx::PipelineHandle{next_pipeline_id_++};
    }

    tgfx::ResourceSetHandle create_bound_resource_set(const tgfx::BoundResourceSetDesc&) override {
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

    int texture_failures_remaining = 0;
    int pipeline_failures_remaining = 0;
    uint32_t create_texture_count = 0;
    uint32_t create_pipeline_count = 0;

private:
    uint32_t next_texture_id_ = 1;
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

    tgfx::PipelineCacheLookupKey key;
    key.vertex_shader = tgfx::ShaderHandle{1};
    key.fragment_shader = tgfx::ShaderHandle{2};
    std::vector<tgfx::VertexLayoutDesc> position_layouts = {
        make_layout(12, "position"),
    };
    key.vertex_layouts = position_layouts;
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

    std::vector<tgfx::VertexLayoutDesc> normal_layouts = {
        make_layout(24, "normal"),
    };
    key.vertex_layouts = normal_layouts;
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

TEST_CASE("PipelineCache retries a failed creation instead of caching an invalid handle") {
    PipelineCacheStatsDevice device;
    device.pipeline_failures_remaining = 1;
    tgfx::PipelineCache cache(device);

    tgfx::PipelineCacheLookupKey key;
    key.vertex_shader = tgfx::ShaderHandle{1};
    key.fragment_shader = tgfx::ShaderHandle{2};
    std::vector<tgfx::VertexLayoutDesc> position_layouts = {
        make_layout(12, "position"),
    };
    key.vertex_layouts = position_layouts;
    key.vertex_layouts_hash = 0x3333;

    CHECK_FALSE(cache.get(key));
    CHECK(cache.size() == 0u);
    CHECK(device.create_pipeline_count == 1u);

    const tgfx::PipelineHandle recovered = cache.get(key);
    CHECK(recovered.id == 1u);
    CHECK(cache.size() == 1u);
    CHECK(device.create_pipeline_count == 2u);
    CHECK(cache.get(key) == recovered);
    CHECK(device.create_pipeline_count == 2u);
}

TEST_CASE("PipelineCache rejects missing required shaders before backend creation") {
    PipelineCacheStatsDevice device;
    tgfx::PipelineCache cache(device);

    tgfx::PipelineCacheLookupKey key;
    key.vertex_shader = tgfx::ShaderHandle{1};
    CHECK_FALSE(cache.get(key));

    key.vertex_shader = {};
    key.fragment_shader = tgfx::ShaderHandle{2};
    CHECK_FALSE(cache.get(key));

    CHECK(device.create_pipeline_count == 0u);
    CHECK(cache.size() == 0u);
    const tgfx::PipelineCacheStats stats = cache.stats();
    CHECK(stats.hit_count == 0u);
    CHECK(stats.miss_count == 0u);
    CHECK(stats.create_pipeline_count == 0u);
}

TEST_CASE("PipelineCache owns layouts after a lookup view expires") {
    PipelineCacheStatsDevice device;
    tgfx::PipelineCache cache(device);

    tgfx::PipelineCacheLookupKey key;
    key.vertex_shader = tgfx::ShaderHandle{1};
    key.fragment_shader = tgfx::ShaderHandle{2};
    key.vertex_layouts_hash = 0x4444;
    {
        std::vector<tgfx::VertexLayoutDesc> transient_layouts = {
            make_layout(12, "position"),
        };
        key.vertex_layouts = transient_layouts;
        CHECK(cache.get(key).id == 1u);
    }

    std::vector<tgfx::VertexLayoutDesc> equivalent_layouts = {
        make_layout(12, "position"),
    };
    key.vertex_layouts = equivalent_layouts;
    CHECK(cache.get(key).id == 1u);
    CHECK(device.create_pipeline_count == 1u);
}

TEST_CASE("PipelineCache compares complete layouts when lookup hashes collide") {
    PipelineCacheStatsDevice device;
    tgfx::PipelineCache cache(device);

    tgfx::PipelineCacheLookupKey key;
    key.vertex_shader = tgfx::ShaderHandle{1};
    key.fragment_shader = tgfx::ShaderHandle{2};
    key.vertex_layouts_hash = 0x5555;

    std::vector<tgfx::VertexLayoutDesc> position_layouts = {
        make_layout(12, "position"),
    };
    key.vertex_layouts = position_layouts;
    const tgfx::PipelineHandle position_pipeline = cache.get(key);
    CHECK(position_pipeline.id == 1u);

    std::vector<tgfx::VertexLayoutDesc> normal_layouts = {
        make_layout(24, "normal"),
    };
    key.vertex_layouts = normal_layouts;
    const tgfx::PipelineHandle normal_pipeline = cache.get(key);
    CHECK(normal_pipeline.id == 2u);
    CHECK(cache.get(key) == normal_pipeline);
    CHECK(device.create_pipeline_count == 2u);
    CHECK(cache.size() == 2u);
}

TEST_CASE("texture pools retry failed allocations using the same key and descriptor") {
    PipelineCacheStatsDevice device;
    tgfx::TextureDesc texture_desc;
    texture_desc.width = 32;
    texture_desc.height = 16;
    texture_desc.usage = tgfx::TextureUsage::Sampled;

    tgfx::TexturePool textures;
    device.texture_failures_remaining = 1;
    CHECK_FALSE(textures.ensure(device, "color", texture_desc));
    CHECK_FALSE(textures.get("color"));
    CHECK(textures.ensure(device, "color", texture_desc));
    CHECK(textures.get("color"));
    CHECK(device.create_texture_count == 2u);

    tgfx::RenderTargetPool targets;
    tgfx::RenderTargetPoolDesc target_desc;
    target_desc.width = 32;
    target_desc.height = 16;
    target_desc.has_depth = true;
    device.texture_failures_remaining = 1;
    CHECK_FALSE(targets.ensure(device, "main", target_desc));
    CHECK_FALSE(targets.color("main"));
    CHECK(targets.ensure(device, "main", target_desc));
    CHECK(targets.color("main"));
    CHECK(targets.depth("main"));
    CHECK(device.create_texture_count == 6u);
}
