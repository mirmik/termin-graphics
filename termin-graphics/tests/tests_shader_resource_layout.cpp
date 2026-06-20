#include "guard_main.h"

#include <cstdio>
#include <cstring>

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

TEST_CASE("shader resource layout preserves D3D11 register placement") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("resource-layout-d3d11-placement-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_binding binding{};
    std::snprintf(binding.name, sizeof(binding.name), "%s", "material");
    binding.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    binding.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    binding.set = 0;
    binding.binding = 1;
    binding.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    binding.has_d3d11_placement = 1;
    binding.d3d11.register_class = TC_SHADER_D3D11_REGISTER_B;
    binding.d3d11.register_index = 3;

    tc_shader_set_resource_layout(shader, &binding, 1);

    const tc_shader_resource_binding* stored =
        tc_shader_find_resource_binding(shader, "material");
    REQUIRE(stored != nullptr);
    CHECK_EQ(stored->has_d3d11_placement, 1u);
    CHECK_EQ(stored->d3d11.register_class, TC_SHADER_D3D11_REGISTER_B);
    CHECK_EQ(stored->d3d11.register_index, 3u);

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("shader resource layout rejects overlapping D3D11 register placement") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("resource-layout-d3d11-conflict-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_binding bindings[2]{};
    std::snprintf(bindings[0].name, sizeof(bindings[0].name), "%s", "material");
    bindings[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    bindings[0].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    bindings[0].set = 0;
    bindings[0].binding = 1;
    bindings[0].stage_mask = TC_SHADER_STAGE_FRAGMENT;
    bindings[0].has_d3d11_placement = 1;
    bindings[0].d3d11.register_class = TC_SHADER_D3D11_REGISTER_B;
    bindings[0].d3d11.register_index = 1;

    std::snprintf(bindings[1].name, sizeof(bindings[1].name), "%s", "draw");
    bindings[1].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    bindings[1].scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    bindings[1].set = 0;
    bindings[1].binding = 2;
    bindings[1].stage_mask = TC_SHADER_STAGE_FRAGMENT;
    bindings[1].has_d3d11_placement = 1;
    bindings[1].d3d11.register_class = TC_SHADER_D3D11_REGISTER_B;
    bindings[1].d3d11.register_index = 1;

    tc_shader_set_resource_layout(shader, bindings, 2);

    CHECK(!tc_shader_has_resource_layout(shader));
    CHECK_EQ(tc_shader_resource_binding_count(shader), 0u);

    bindings[1].stage_mask = TC_SHADER_STAGE_VERTEX;
    tc_shader_set_resource_layout(shader, bindings, 2);

    CHECK(tc_shader_has_resource_layout(shader));
    CHECK_EQ(tc_shader_resource_binding_count(shader), 2u);

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

TEST_CASE("shader source identity includes explicit stage entry points") {
    tc_shader_init();

    const char* vertex_source = R"(
import termin_prelude;
struct VertexOutput { float4 position : SV_Position; };
[shader("vertex")] VertexOutput vs_main() {
    VertexOutput output;
    output.position = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
[shader("vertex")] VertexOutput vs_alt() {
    VertexOutput output;
    output.position = float4(1.0, 0.0, 0.0, 1.0);
    return output;
}
)";
    const char* fragment_source = R"(
struct FragmentOutput { float4 color : SV_Target0; };
[shader("fragment")] FragmentOutput fs_main() {
    FragmentOutput output;
    output.color = float4(1.0);
    return output;
}
)";

    tc_shader_handle first = tc_shader_from_sources_with_entries_ex(
        vertex_source,
        fragment_source,
        nullptr,
        "entry-point-identity-test",
        nullptr,
        nullptr,
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED,
        "vs_main",
        "fs_main",
        nullptr);
    REQUIRE(!tc_shader_handle_is_invalid(first));

    tc_shader_handle second = tc_shader_from_sources_with_entries_ex(
        vertex_source,
        fragment_source,
        nullptr,
        "entry-point-identity-test-alt",
        nullptr,
        nullptr,
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED,
        "vs_alt",
        "fs_main",
        nullptr);
    REQUIRE(!tc_shader_handle_is_invalid(second));
    CHECK(first.index != second.index);

    tc_shader* first_shader = tc_shader_get(first);
    tc_shader* second_shader = tc_shader_get(second);
    REQUIRE(first_shader != nullptr);
    REQUIRE(second_shader != nullptr);
    REQUIRE(first_shader->vertex_entry != nullptr);
    REQUIRE(first_shader->fragment_entry != nullptr);
    REQUIRE(second_shader->vertex_entry != nullptr);
    CHECK(std::strcmp(first_shader->vertex_entry, "vs_main") == 0);
    CHECK(std::strcmp(first_shader->fragment_entry, "fs_main") == 0);
    CHECK(std::strcmp(second_shader->vertex_entry, "vs_alt") == 0);
    CHECK(std::strcmp(first_shader->source_hash, second_shader->source_hash) != 0);

    tc_shader_destroy(first);
    tc_shader_destroy(second);
    tc_shader_shutdown();
}
