#include "guard_main.h"

#include "tgfx/resources/tc_mesh.h"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/tc_mesh_bridge.hpp"

#include <memory>

namespace {

class FakeRenderDevice final : public tgfx::IRenderDevice {
public:
    int create_buffer_count = 0;
    int upload_buffer_count = 0;
    int ensure_mesh_count = 0;

    tgfx::BackendType backend_type() const override { return tgfx::BackendType::Null; }
    tgfx::BackendCapabilities capabilities() const override { return {}; }
    void wait_idle() override {}

    tgfx::BufferHandle create_buffer(const tgfx::BufferDesc&) override {
        ++create_buffer_count;
        return tgfx::BufferHandle{100u + static_cast<uint32_t>(create_buffer_count)};
    }
    tgfx::TextureHandle create_texture(const tgfx::TextureDesc&) override { return {}; }
    tgfx::SamplerHandle create_sampler(const tgfx::SamplerDesc&) override { return {}; }
    tgfx::ShaderHandle create_shader(const tgfx::ShaderDesc&) override { return {}; }
    tgfx::PipelineHandle create_pipeline(const tgfx::PipelineDesc&) override { return {}; }
    tgfx::ResourceSetHandle create_resource_set(const tgfx::ResourceSetDesc&) override { return {}; }

    void destroy(tgfx::BufferHandle) override {}
    void destroy(tgfx::TextureHandle) override {}
    void destroy(tgfx::SamplerHandle) override {}
    void destroy(tgfx::ShaderHandle) override {}
    void destroy(tgfx::PipelineHandle) override {}
    void destroy(tgfx::ResourceSetHandle) override {}

    void upload_buffer(tgfx::BufferHandle, std::span<const uint8_t>, uint64_t = 0) override {
        ++upload_buffer_count;
    }
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
    tgfx::TextureDesc texture_desc(tgfx::TextureHandle) const override { return {}; }

    std::unique_ptr<tgfx::ICommandList> create_command_list(tgfx::QueueType = tgfx::QueueType::Graphics) override {
        return {};
    }
    void submit(tgfx::ICommandList&) override {}
    void present() override {}

    std::pair<tgfx::BufferHandle, tgfx::BufferHandle> ensure_tc_mesh(tc_mesh*) override {
        ++ensure_mesh_count;
        return {tgfx::BufferHandle{1}, tgfx::BufferHandle{2}};
    }
};

} // namespace

TEST_CASE("filter vertex layout by semantic names") {
    tgfx::VertexBufferLayout layout;
    layout.stride = 80;
    layout.attributes.push_back({0, tgfx::VertexFormat::Float3, 0, "position"});
    layout.attributes.push_back({1, tgfx::VertexFormat::Float3, 12, "normal"});
    layout.attributes.push_back({4, tgfx::VertexFormat::Float4, 48, "joints"});
    layout.attributes.push_back({5, tgfx::VertexFormat::Float4, 64, "weights"});

    tgfx::VertexBufferLayout filtered =
        tgfx::filter_vertex_layout_to_semantics(
            layout,
            {"position", "joints", "weights"},
            true);

    CHECK_EQ(filtered.stride, 80u);
    CHECK(filtered.use_shader_input_locations);
    REQUIRE_EQ(filtered.attributes.size(), 3u);
    CHECK_EQ(filtered.attributes[0].semantic, "position");
    CHECK_EQ(filtered.attributes[1].semantic, "joints");
    CHECK_EQ(filtered.attributes[2].semantic, "weights");
}

TEST_CASE("semantic filter falls back to standard locations") {
    tgfx::VertexBufferLayout layout;
    layout.stride = 24;
    layout.attributes.push_back({0, tgfx::VertexFormat::Float3, 0});
    layout.attributes.push_back({1, tgfx::VertexFormat::Float3, 12});

    tgfx::VertexBufferLayout filtered =
        tgfx::filter_vertex_layout_to_semantics(
            layout,
            {"position", "normal"});

    REQUIRE_EQ(filtered.attributes.size(), 2u);
    CHECK_EQ(filtered.attributes[0].location, 0u);
    CHECK_EQ(filtered.attributes[1].location, 1u);
}

TEST_CASE("vertex semantic helpers prefer explicit names over standard locations") {
    tgfx::VertexAttribute position{0, tgfx::VertexFormat::Float3, 0};
    tgfx::VertexAttribute custom{0, tgfx::VertexFormat::Float3, 0, "custom_position"};

    CHECK_EQ(tgfx::standard_vertex_semantic_for_location(0), "position");
    CHECK_EQ(tgfx::vertex_attribute_semantic(position), "position");
    CHECK_EQ(tgfx::vertex_attribute_semantic(custom), "custom_position");

    tgfx::VertexBufferLayout layout;
    layout.attributes.push_back(position);
    layout.attributes.push_back({2, tgfx::VertexFormat::Float2, 12});

    CHECK(tgfx::vertex_layout_has_semantic(layout, "position"));
    CHECK(tgfx::vertex_layout_has_semantic(layout, "uv"));
    CHECK_FALSE(tgfx::vertex_layout_has_semantic(layout, "normal"));
}

TEST_CASE("wrap mesh keeps canonical layout without per-draw augmentation") {
    tc_mesh mesh{};
    mesh.layout = tc_vertex_layout_pos_normal_uv();
    mesh.index_count = 3;
    mesh.draw_mode = TC_DRAW_TRIANGLES;
    mesh.header.name = const_cast<char*>("bridge-test");

    FakeRenderDevice device;
    tgfx::Tgfx2MeshBinding binding = tgfx::wrap_mesh_as_tgfx2(device, &mesh);

    CHECK_EQ(device.ensure_mesh_count, 1);
    CHECK_EQ(device.create_buffer_count, 0);
    CHECK_EQ(device.upload_buffer_count, 0);
    CHECK(binding.vertex_buffer);
    CHECK(binding.index_buffer);
    CHECK_FALSE(binding.destroy_vertex_buffer);
    CHECK_EQ(binding.layout.stride, mesh.layout.stride);
    CHECK(tgfx::vertex_layout_has_semantic(binding.layout, "position"));
    CHECK(tgfx::vertex_layout_has_semantic(binding.layout, "normal"));
    CHECK(tgfx::vertex_layout_has_semantic(binding.layout, "uv"));
    CHECK_FALSE(tgfx::vertex_layout_has_semantic(binding.layout, "tangent"));
}
