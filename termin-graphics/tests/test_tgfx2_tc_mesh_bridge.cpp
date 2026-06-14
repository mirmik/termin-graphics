#include "guard_main.h"

#include "tgfx2/tc_mesh_bridge.hpp"

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
