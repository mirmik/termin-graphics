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

TEST_CASE("shader contract attaches deep-copied draw contract") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-contract-attach-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);
    CHECK(!tc_shader_has_contract(shader));

    tc_shader_contract_vertex_input vertex_inputs[2]{};
    std::snprintf(vertex_inputs[0].semantic, sizeof(vertex_inputs[0].semantic), "%s", "position");
    vertex_inputs[0].type = TC_SHADER_CONTRACT_VALUE_FLOAT3;
    vertex_inputs[0].required = 1;
    std::snprintf(vertex_inputs[1].semantic, sizeof(vertex_inputs[1].semantic), "%s", "normal");
    vertex_inputs[1].type = TC_SHADER_CONTRACT_VALUE_FLOAT3;
    vertex_inputs[1].required = 1;

    tc_shader_contract_storage_buffer storage{};
    std::snprintf(storage.resource_name, sizeof(storage.resource_name), "%s", "foliage_instances");
    storage.stride = 32;

    tc_shader_resource_binding resources[2]{};
    std::snprintf(resources[0].name, sizeof(resources[0].name), "%s", "per_frame");
    resources[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    resources[0].scope = TC_SHADER_RESOURCE_SCOPE_FRAME;
    resources[0].set = 0;
    resources[0].binding = 2;
    resources[0].stage_mask = TC_SHADER_STAGE_VERTEX;
    std::snprintf(resources[1].name, sizeof(resources[1].name), "%s", "foliage_instances");
    resources[1].kind = TC_SHADER_RESOURCE_STORAGE_BUFFER;
    resources[1].scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    resources[1].set = 0;
    resources[1].binding = 25;
    resources[1].stage_mask = TC_SHADER_STAGE_VERTEX;

    tc_shader_contract_desc desc{};
    desc.schema_version = TC_SHADER_CONTRACT_SCHEMA_VERSION;
    desc.producer_kind = TC_SHADER_CONTRACT_PRODUCER_MATERIAL_PIPELINE;
    desc.draw_kind = TC_SHADER_CONTRACT_DRAW_INSTANCED_MESH;
    desc.vertex_inputs = vertex_inputs;
    desc.vertex_input_count = 2;
    desc.storage_buffers = &storage;
    desc.storage_buffer_count = 1;
    desc.resources = resources;
    desc.resource_count = 2;
    desc.debug_name = "foliage material shader";
    desc.producer_debug_name = "material pipeline";

    REQUIRE(tc_shader_set_contract(shader, &desc));
    CHECK(tc_shader_has_contract(shader));

    std::snprintf(vertex_inputs[0].semantic, sizeof(vertex_inputs[0].semantic), "%s", "mutated");
    std::snprintf(resources[0].name, sizeof(resources[0].name), "%s", "mutated_resource");

    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(shader, &view));
    CHECK_EQ(view.schema_version, TC_SHADER_CONTRACT_SCHEMA_VERSION);
    CHECK_EQ(view.producer_kind, TC_SHADER_CONTRACT_PRODUCER_MATERIAL_PIPELINE);
    CHECK_EQ(view.draw_kind, TC_SHADER_CONTRACT_DRAW_INSTANCED_MESH);
    CHECK_EQ(view.shader.index, handle.index);
    CHECK_EQ(view.shader.generation, handle.generation);
    REQUIRE_EQ(view.vertex_input_count, 2u);
    CHECK(std::strcmp(view.vertex_inputs[0].semantic, "position") == 0);
    CHECK_EQ(view.vertex_inputs[0].type, TC_SHADER_CONTRACT_VALUE_FLOAT3);
    CHECK_EQ(view.storage_buffer_count, 1u);
    CHECK(std::strcmp(view.storage_buffers[0].resource_name, "foliage_instances") == 0);
    CHECK_EQ(view.storage_buffers[0].stride, 32u);
    REQUIRE_EQ(view.resource_count, 2u);
    bool has_per_frame = false;
    for (uint32_t i = 0; i < view.resource_count; ++i) {
        if (std::strcmp(view.resources[i].name, "per_frame") == 0) {
            has_per_frame = true;
        }
    }
    CHECK(has_per_frame);
    CHECK(std::strcmp(view.debug_name, "foliage material shader") == 0);
    CHECK(std::strcmp(view.producer_debug_name, "material pipeline") == 0);

    tc_shader_clear_contract(shader);
    CHECK(!tc_shader_has_contract(shader));
    CHECK(!tc_shader_get_contract_view(shader, &view));

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("shader contract clears when shader sources change") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-contract-source-reset-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_contract_vertex_input vertex_input{};
    std::snprintf(vertex_input.semantic, sizeof(vertex_input.semantic), "%s", "position");
    vertex_input.type = TC_SHADER_CONTRACT_VALUE_FLOAT3;
    vertex_input.required = 1;

    tc_shader_contract_desc desc{};
    desc.producer_kind = TC_SHADER_CONTRACT_PRODUCER_ENGINE_GENERATED;
    desc.draw_kind = TC_SHADER_CONTRACT_DRAW_MESH;
    desc.vertex_inputs = &vertex_input;
    desc.vertex_input_count = 1;
    REQUIRE(tc_shader_set_contract(shader, &desc));
    CHECK(tc_shader_has_contract(shader));

    REQUIRE(tc_shader_set_sources(
        shader,
        "void main() {}",
        "void main() {}",
        nullptr,
        "contract reset shader",
        nullptr));
    CHECK(!tc_shader_has_contract(shader));

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("shader contract resources follow shader resource layout updates") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-contract-resource-sync-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_contract_vertex_input vertex_input{};
    std::snprintf(vertex_input.semantic, sizeof(vertex_input.semantic), "%s", "position");
    vertex_input.type = TC_SHADER_CONTRACT_VALUE_FLOAT3;
    vertex_input.required = 1;

    tc_shader_contract_desc desc{};
    desc.producer_kind = TC_SHADER_CONTRACT_PRODUCER_SHADER_PARSER;
    desc.draw_kind = TC_SHADER_CONTRACT_DRAW_MESH;
    desc.vertex_inputs = &vertex_input;
    desc.vertex_input_count = 1;
    REQUIRE(tc_shader_set_contract(shader, &desc));

    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(shader, &view));
    CHECK_EQ(view.resource_count, 0u);

    tc_shader_resource_binding resource{};
    std::snprintf(resource.name, sizeof(resource.name), "%s", "material");
    resource.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    resource.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    resource.set = 0;
    resource.binding = 1;
    resource.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    resource.size = 64;
    tc_shader_set_resource_layout(shader, &resource, 1);

    REQUIRE(tc_shader_get_contract_view(shader, &view));
    REQUIRE_EQ(view.resource_count, 1u);
    CHECK(std::strcmp(view.resources[0].name, "material") == 0);
    CHECK_EQ(view.resources[0].binding, 1u);

    tc_shader_set_resource_layout(shader, nullptr, 0);
    REQUIRE(tc_shader_get_contract_view(shader, &view));
    CHECK_EQ(view.resource_count, 0u);

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("material UBO layout update preserves D3D11 register placement") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("material-layout-d3d11-placement-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_binding binding{};
    std::snprintf(binding.name, sizeof(binding.name), "%s", "material");
    binding.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    binding.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    binding.set = 0;
    binding.binding = 1;
    binding.stage_mask = TC_SHADER_STAGE_ALL_GRAPHICS;
    binding.size = 192;
    binding.has_d3d11_placement = 1;
    binding.d3d11.register_class = TC_SHADER_D3D11_REGISTER_B;
    binding.d3d11.register_index = 0;

    tc_shader_set_resource_layout(shader, &binding, 1);

    tc_material_ubo_entry entry{};
    std::snprintf(entry.name, sizeof(entry.name), "%s", "u_skybox_type");
    std::snprintf(entry.property_type, sizeof(entry.property_type), "%s", "Int");
    entry.offset = 128;
    entry.size = 4;
    tc_shader_set_material_ubo_layout(shader, &entry, 1, 192);

    const tc_shader_resource_binding* stored =
        tc_shader_find_resource_binding(shader, "material");
    REQUIRE(stored != nullptr);
    CHECK_EQ(stored->has_d3d11_placement, 1u);
    CHECK_EQ(stored->d3d11.register_class, TC_SHADER_D3D11_REGISTER_B);
    CHECK_EQ(stored->d3d11.register_index, 0u);
    CHECK_EQ(stored->size, 192u);

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
