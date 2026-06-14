#include "guard_main.h"

#include <cstdio>

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

TEST_CASE("shader resource layout presence distinguishes known empty layout") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("resource-layout-presence-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);
    CHECK(!tc_shader_has_resource_layout(shader));
    CHECK_EQ(tc_shader_resource_binding_count(shader), 0u);

    tc_shader_mark_resource_layout_known(shader);
    CHECK(tc_shader_has_resource_layout(shader));
    CHECK_EQ(tc_shader_resource_binding_count(shader), 0u);

    tc_shader_resource_binding binding{};
    std::snprintf(binding.name, sizeof(binding.name), "%s", "u_shadow_map");
    binding.kind = TC_SHADER_RESOURCE_TEXTURE;
    binding.scope = TC_SHADER_RESOURCE_SCOPE_PASS;
    binding.set = 0;
    binding.binding = 8;
    binding.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    tc_shader_set_resource_layout(shader, &binding, 1);

    CHECK(tc_shader_has_resource_layout(shader));
    CHECK_EQ(tc_shader_resource_binding_count(shader), 1u);
    REQUIRE(tc_shader_find_resource_binding(shader, "u_shadow_map") != nullptr);

    tc_shader_set_resource_layout(shader, nullptr, 0);
    CHECK(!tc_shader_has_resource_layout(shader));
    CHECK_EQ(tc_shader_resource_binding_count(shader), 0u);

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("raw rewritten GLSL source infers compact engine resource layout") {
    tc_shader_init();

    const char* vertex_source = R"(
#version 450 core
layout(std140, binding = 2) uniform PerFrame {
    mat4 u_view;
    mat4 u_projection;
};
layout(std140, binding = 24) uniform DrawData {
    mat4 _u_model;
} draw_data;
layout(location = 0) in vec3 in_position;
void main() {
    gl_Position = u_projection * u_view * draw_data._u_model * vec4(in_position, 1.0);
}
)";
    const char* fragment_source = R"(
#version 450 core
layout(location = 0) out vec4 out_color;
void main() {
    out_color = vec4(1.0);
}
)";

    tc_shader_handle handle = tc_shader_from_sources_ex(
        vertex_source,
        fragment_source,
        nullptr,
        "raw-glsl-engine-layout-test",
        nullptr,
        "raw-glsl-engine-layout-test",
        TC_SHADER_LANGUAGE_GLSL,
        TC_SHADER_ARTIFACT_OPTIONAL);
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);
    CHECK(tc_shader_has_resource_layout(shader));
    CHECK_EQ(tc_shader_resource_binding_count(shader), 2u);

    const tc_shader_resource_binding* per_frame =
        tc_shader_find_resource_binding(shader, TC_SHADER_RESOURCE_PER_FRAME);
    REQUIRE(per_frame != nullptr);
    CHECK_EQ(per_frame->kind, TC_SHADER_RESOURCE_CONSTANT_BUFFER);
    CHECK_EQ(per_frame->scope, TC_SHADER_RESOURCE_SCOPE_FRAME);
    CHECK_EQ(per_frame->binding, 2u);

    const tc_shader_resource_binding* draw =
        tc_shader_find_resource_binding(shader, TC_SHADER_RESOURCE_DRAW);
    REQUIRE(draw != nullptr);
    CHECK_EQ(draw->kind, TC_SHADER_RESOURCE_CONSTANT_BUFFER);
    CHECK_EQ(draw->scope, TC_SHADER_RESOURCE_SCOPE_DRAW);
    CHECK_EQ(draw->binding, 24u);

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}
