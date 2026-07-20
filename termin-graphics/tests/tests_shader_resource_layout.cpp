#include "guard_main.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include <tgfx/resources/tc_shader_abi.h>
#include <tgfx/resources/tc_shader_registry.h>
}

TEST_CASE("shader ABI C API exposes canonical names and legacy aliases") {
    const tc_shader_abi_resource_decl* draw =
        tc_shader_abi_find_resource(TC_SHADER_RESOURCE_DRAW);
    REQUIRE(draw != nullptr);
    CHECK_EQ(draw->id, static_cast<uint32_t>(TC_SHADER_ABI_RESOURCE_DRAW_DATA));
    CHECK(std::strcmp(draw->canonical_name, TC_SHADER_RESOURCE_DRAW_DATA) == 0);
    CHECK_EQ(draw->kind, static_cast<uint32_t>(TC_SHADER_RESOURCE_CONSTANT_BUFFER));
    CHECK_EQ(draw->scope, static_cast<uint32_t>(TC_SHADER_RESOURCE_SCOPE_DRAW));
    CHECK(tc_shader_abi_name_is_legacy_alias(draw, TC_SHADER_RESOURCE_DRAW));
    CHECK(!tc_shader_abi_name_is_legacy_alias(draw, TC_SHADER_RESOURCE_DRAW_DATA));

    const tc_shader_abi_resource_decl* bone =
        tc_shader_abi_find_resource("BoneBlock");
    REQUIRE(bone != nullptr);
    CHECK_EQ(bone->id, static_cast<uint32_t>(TC_SHADER_ABI_RESOURCE_BONE_BLOCK));
    CHECK(std::strcmp(bone->canonical_name, TC_SHADER_RESOURCE_BONE_BLOCK) == 0);
    CHECK(tc_shader_abi_name_is_legacy_alias(bone, "BoneBlock"));

    tc_shader_resource_binding binding{};
    std::snprintf(binding.name, sizeof(binding.name), "%s", "BoneBlock");
    binding.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    binding.scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    CHECK(tc_shader_abi_binding_matches(bone, &binding));

    binding.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    CHECK(!tc_shader_abi_binding_matches(bone, &binding));
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

TEST_CASE("shader resource layout stores backend-dependent set binding overlaps") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("resource-layout-backend-dependent-overlap-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_binding bindings[2]{};
    std::snprintf(bindings[0].name, sizeof(bindings[0].name), "%s", "material");
    bindings[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    bindings[0].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    bindings[0].set = 0;
    bindings[0].binding = 4;
    bindings[0].stage_mask = TC_SHADER_STAGE_FRAGMENT;

    std::snprintf(bindings[1].name, sizeof(bindings[1].name), "%s", "albedo_texture");
    bindings[1].kind = TC_SHADER_RESOURCE_TEXTURE;
    bindings[1].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    bindings[1].set = 0;
    bindings[1].binding = 4;
    bindings[1].stage_mask = TC_SHADER_STAGE_FRAGMENT;

    tc_shader_set_resource_layout(shader, bindings, 2);

    CHECK(tc_shader_has_resource_layout(shader));
    CHECK_EQ(tc_shader_resource_binding_count(shader), 2u);
    CHECK(tc_shader_find_resource_binding(shader, "material") != nullptr);
    CHECK(tc_shader_find_resource_binding(shader, "albedo_texture") != nullptr);

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("shader contract attaches deep-copied interface contract") {
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

    tc_shader_resource_requirement resources[2]{};
    std::snprintf(resources[0].name, sizeof(resources[0].name), "%s", "per_frame");
    resources[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    resources[0].scope = TC_SHADER_RESOURCE_SCOPE_FRAME;
    resources[0].stage_mask = TC_SHADER_STAGE_VERTEX;
    std::snprintf(resources[1].name, sizeof(resources[1].name), "%s", "foliage_instances");
    resources[1].kind = TC_SHADER_RESOURCE_STORAGE_BUFFER;
    resources[1].scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    resources[1].stage_mask = TC_SHADER_STAGE_VERTEX;
    resources[1].element_stride = 32;

    tc_shader_contract_desc desc{};
    desc.schema_version = TC_SHADER_CONTRACT_SCHEMA_VERSION;
    desc.source_kind = TC_SHADER_CONTRACT_SOURCE_ASSEMBLED;
    desc.vertex_inputs = vertex_inputs;
    desc.vertex_input_count = 2;
    desc.resources = resources;
    desc.resource_count = 2;
    desc.debug_name = "foliage material shader";
    desc.source_debug_name = "material pipeline";

    REQUIRE(tc_shader_set_contract(shader, &desc));
    CHECK(tc_shader_has_contract(shader));

    std::snprintf(vertex_inputs[0].semantic, sizeof(vertex_inputs[0].semantic), "%s", "mutated");
    std::snprintf(resources[0].name, sizeof(resources[0].name), "%s", "mutated_resource");

    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(shader, &view));
    CHECK_EQ(view.schema_version, TC_SHADER_CONTRACT_SCHEMA_VERSION);
    CHECK_EQ(view.source_kind, TC_SHADER_CONTRACT_SOURCE_ASSEMBLED);
    CHECK_EQ(view.shader.index, handle.index);
    CHECK_EQ(view.shader.generation, handle.generation);
    REQUIRE_EQ(view.vertex_input_count, 2u);
    CHECK(std::strcmp(view.vertex_inputs[0].semantic, "position") == 0);
    CHECK_EQ(view.vertex_inputs[0].type, TC_SHADER_CONTRACT_VALUE_FLOAT3);
    REQUIRE_EQ(view.resource_count, 2u);
    bool has_per_frame = false;
    bool has_foliage_instances = false;
    for (uint32_t i = 0; i < view.resource_count; ++i) {
        if (std::strcmp(view.resources[i].name, "per_frame") == 0) {
            has_per_frame = true;
        }
        if (std::strcmp(view.resources[i].name, "foliage_instances") == 0) {
            has_foliage_instances = true;
            CHECK_EQ(view.resources[i].element_stride, 32u);
        }
    }
    CHECK(has_per_frame);
    CHECK(has_foliage_instances);
    CHECK(std::strcmp(view.debug_name, "foliage material shader") == 0);
    CHECK(std::strcmp(view.source_debug_name, "material pipeline") == 0);

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
    desc.source_kind = TC_SHADER_CONTRACT_SOURCE_GENERATED;
    desc.vertex_inputs = &vertex_input;
    desc.vertex_input_count = 1;
    REQUIRE(tc_shader_set_contract(shader, &desc));
    CHECK(tc_shader_has_contract(shader));
    REQUIRE(tc_shader_set_language(shader, TC_SHADER_LANGUAGE_GLSL));

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

TEST_CASE("shader contract resources are independent from shader resource layout updates") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-contract-resource-independence-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_contract_vertex_input vertex_input{};
    std::snprintf(vertex_input.semantic, sizeof(vertex_input.semantic), "%s", "position");
    vertex_input.type = TC_SHADER_CONTRACT_VALUE_FLOAT3;
    vertex_input.required = 1;

    tc_shader_contract_desc desc{};
    desc.source_kind = TC_SHADER_CONTRACT_SOURCE_DECLARED;
    desc.vertex_inputs = &vertex_input;
    desc.vertex_input_count = 1;
    tc_shader_resource_requirement requirement{};
    std::snprintf(requirement.name, sizeof(requirement.name), "%s", "declared_material");
    requirement.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    requirement.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    requirement.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    requirement.size = 32;
    desc.resources = &requirement;
    desc.resource_count = 1;
    REQUIRE(tc_shader_set_contract(shader, &desc));

    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(shader, &view));
    REQUIRE_EQ(view.resource_count, 1u);
    CHECK(std::strcmp(view.resources[0].name, "declared_material") == 0);
    CHECK_EQ(view.resources[0].size, 32u);

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
    CHECK(std::strcmp(view.resources[0].name, "declared_material") == 0);
    CHECK_EQ(view.resources[0].kind, TC_SHADER_RESOURCE_CONSTANT_BUFFER);
    CHECK_EQ(view.resources[0].scope, TC_SHADER_RESOURCE_SCOPE_MATERIAL);
    CHECK_EQ(view.resources[0].size, 32u);

    tc_shader_set_resource_layout(shader, nullptr, 0);
    REQUIRE(tc_shader_get_contract_view(shader, &view));
    REQUIRE_EQ(view.resource_count, 1u);
    CHECK(std::strcmp(view.resources[0].name, "declared_material") == 0);
    CHECK_EQ(view.resources[0].size, 32u);

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("declared shader contract resources synchronize from compiler layout explicitly") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("declared-contract-reflection-sync-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_contract_vertex_input vertex_input{};
    std::snprintf(vertex_input.semantic, sizeof(vertex_input.semantic), "%s", "position");
    vertex_input.type = TC_SHADER_CONTRACT_VALUE_FLOAT3;
    vertex_input.required = 1;

    tc_shader_resource_requirement declared{};
    std::snprintf(declared.name, sizeof(declared.name), "%s", "material");
    declared.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    declared.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    declared.stage_mask = TC_SHADER_STAGE_ALL_GRAPHICS;
    declared.size = 224;

    tc_shader_contract_desc desc{};
    desc.source_kind = TC_SHADER_CONTRACT_SOURCE_DECLARED;
    desc.vertex_inputs = &vertex_input;
    desc.vertex_input_count = 1;
    desc.resources = &declared;
    desc.resource_count = 1;
    desc.debug_name = "declared-contract-reflection-sync-test";
    desc.source_debug_name = "test parser";
    REQUIRE(tc_shader_set_contract(shader, &desc));

    tc_shader_resource_binding fragment_resources[2]{};
    std::snprintf(
        fragment_resources[0].name,
        sizeof(fragment_resources[0].name),
        "%s",
        "material");
    fragment_resources[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    fragment_resources[0].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    fragment_resources[0].stage_mask = TC_SHADER_STAGE_FRAGMENT;
    fragment_resources[0].size = 224;
    std::snprintf(
        fragment_resources[1].name,
        sizeof(fragment_resources[1].name),
        "%s",
        "u_color_texture");
    fragment_resources[1].kind = TC_SHADER_RESOURCE_TEXTURE;
    fragment_resources[1].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    fragment_resources[1].stage_mask = TC_SHADER_STAGE_FRAGMENT;
    tc_shader_set_resource_layout(shader, fragment_resources, 2);

    REQUIRE(tc_shader_sync_reflected_contract_resources(shader));
    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(shader, &view));
    CHECK_EQ(view.source_kind, TC_SHADER_CONTRACT_SOURCE_DECLARED);
    REQUIRE_EQ(view.vertex_input_count, 1u);
    CHECK(std::strcmp(view.vertex_inputs[0].semantic, "position") == 0);
    REQUIRE_EQ(view.resource_count, 2u);
    CHECK_EQ(view.resources[0].stage_mask, TC_SHADER_STAGE_FRAGMENT);
    CHECK_EQ(view.resources[1].stage_mask, TC_SHADER_STAGE_FRAGMENT);

    tc_shader_resource_binding merged_resources[3]{};
    merged_resources[0] = fragment_resources[0];
    merged_resources[1] = fragment_resources[1];
    std::snprintf(
        merged_resources[2].name,
        sizeof(merged_resources[2].name),
        "%s",
        "draw_data");
    merged_resources[2].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    merged_resources[2].scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    merged_resources[2].stage_mask = TC_SHADER_STAGE_VERTEX;
    merged_resources[2].size = 64;
    tc_shader_set_resource_layout(shader, merged_resources, 3);

    REQUIRE(tc_shader_sync_reflected_contract_resources(shader));
    REQUIRE(tc_shader_get_contract_view(shader, &view));
    REQUIRE_EQ(view.resource_count, 3u);
    bool found_vertex_resource = false;
    for (uint32_t i = 0; i < view.resource_count; ++i) {
        if (std::strcmp(view.resources[i].name, "draw_data") == 0) {
            found_vertex_resource = true;
            CHECK_EQ(view.resources[i].stage_mask, TC_SHADER_STAGE_VERTEX);
        }
    }
    CHECK(found_vertex_resource);

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("assembled shader contract resources are not replaced by reflection sync") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("assembled-contract-reflection-sync-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_requirement assembled{};
    std::snprintf(assembled.name, sizeof(assembled.name), "%s", "assembled_only");
    assembled.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    assembled.scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    assembled.stage_mask = TC_SHADER_STAGE_VERTEX;
    tc_shader_contract_desc desc{};
    desc.source_kind = TC_SHADER_CONTRACT_SOURCE_ASSEMBLED;
    desc.resources = &assembled;
    desc.resource_count = 1;
    REQUIRE(tc_shader_set_contract(shader, &desc));

    tc_shader_resource_binding reflected{};
    std::snprintf(reflected.name, sizeof(reflected.name), "%s", "reflected_only");
    reflected.kind = TC_SHADER_RESOURCE_TEXTURE;
    reflected.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    reflected.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    tc_shader_set_resource_layout(shader, &reflected, 1);

    REQUIRE(tc_shader_sync_reflected_contract_resources(shader));
    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(shader, &view));
    REQUIRE_EQ(view.resource_count, 1u);
    CHECK(std::strcmp(view.resources[0].name, "assembled_only") == 0);

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

    std::snprintf(bindings[1].name, sizeof(bindings[1].name), "%s", TC_SHADER_RESOURCE_DRAW_DATA);
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

TEST_CASE("raw GLSL source does not infer compact engine resource layout") {
    tc_shader_init();

    const char* vertex_source = R"(
#version 450 core
layout(std140, binding = 2) uniform PerFrame {
    mat4 u_view;
    mat4 u_projection;
};
layout(std140, binding = 7) uniform DrawData {
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

    const tc_shader_create_desc shader_desc = {
        {
            vertex_source,
            fragment_source,
            nullptr,
            "raw-glsl-engine-layout-test",
            nullptr,
            nullptr,
            nullptr,
            nullptr
        },
        "raw-glsl-engine-layout-test",
        TC_SHADER_LANGUAGE_GLSL,
        TC_SHADER_ARTIFACT_OPTIONAL
    };
    tc_shader_handle handle = tc_shader_from_sources_desc(&shader_desc);
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);
    CHECK(!tc_shader_has_resource_layout(shader));
    CHECK_EQ(tc_shader_resource_binding_count(shader), 0u);
    CHECK(tc_shader_find_resource_binding(shader, TC_SHADER_RESOURCE_PER_FRAME) == nullptr);
    CHECK(tc_shader_find_resource_binding(shader, TC_SHADER_RESOURCE_DRAW_DATA) == nullptr);

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

    const tc_shader_create_desc first_desc = {
        {
            vertex_source,
            fragment_source,
            nullptr,
            "entry-point-identity-test",
            nullptr,
            "vs_main",
            "fs_main",
            nullptr
        },
        nullptr,
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    };
    tc_shader_handle first = tc_shader_from_sources_desc(&first_desc);
    REQUIRE(!tc_shader_handle_is_invalid(first));

    const tc_shader_create_desc second_desc = {
        {
            vertex_source,
            fragment_source,
            nullptr,
            "entry-point-identity-test-alt",
            nullptr,
            "vs_alt",
            "fs_main",
            nullptr
        },
        nullptr,
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    };
    tc_shader_handle second = tc_shader_from_sources_desc(&second_desc);
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
