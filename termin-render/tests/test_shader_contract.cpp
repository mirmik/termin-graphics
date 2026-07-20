#include "guard_main.h"

GUARD_TEST_MAIN();

#include <cstdio>

#include <termin/render/shader_contract.hpp>

namespace {

tc_shader_resource_requirement material_requirement()
{
    tc_shader_resource_requirement requirement{};
    std::snprintf(
        requirement.name,
        sizeof(requirement.name),
        "%s",
        TC_SHADER_RESOURCE_MATERIAL);
    requirement.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    requirement.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    requirement.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    requirement.size = 64;
    return requirement;
}

tc_shader_resource_binding material_binding()
{
    tc_shader_resource_binding binding{};
    std::snprintf(
        binding.name,
        sizeof(binding.name),
        "%s",
        TC_SHADER_RESOURCE_MATERIAL);
    binding.kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    binding.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    binding.set = TC_SHADER_RESOURCE_SET_DEFAULT;
    binding.binding = 1;
    binding.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    binding.size = 64;
    return binding;
}

tc_shader_contract_desc contract_desc(
    const tc_shader_resource_requirement* requirement,
    uint32_t requirement_count)
{
    tc_shader_contract_desc desc{};
    desc.schema_version = TC_SHADER_CONTRACT_SCHEMA_VERSION;
    desc.source_kind = TC_SHADER_CONTRACT_SOURCE_DECLARED;
    desc.resources = requirement;
    desc.resource_count = requirement_count;
    desc.debug_name = "shader-contract-test";
    desc.source_debug_name = "test";
    return desc;
}

} // namespace

TEST_CASE("shader contract validation allows explicit legacy shader without contract") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-contract-legacy-allowed");
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    termin::ShaderContractValidationOptions options{};
    options.debug_context = "contract-test";
    options.require_contract = false;
    CHECK(termin::validate_shader_contract(shader, options));

    options.require_contract = true;
    CHECK(!termin::validate_shader_contract(shader, options));

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("shader contract validation accepts matching resource layout") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-contract-layout-valid");
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_requirement requirement = material_requirement();
    tc_shader_contract_desc desc = contract_desc(&requirement, 1);
    REQUIRE(tc_shader_set_contract(shader, &desc));

    tc_shader_resource_binding binding = material_binding();
    tc_shader_set_resource_layout(shader, &binding, 1);

    termin::ShaderContractValidationOptions options{};
    options.debug_context = "contract-test";
    options.require_contract = true;
    CHECK(termin::validate_shader_contract(shader, options));

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("shader contract validation rejects missing resource layout entry") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-contract-layout-missing");
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_requirement requirement = material_requirement();
    tc_shader_contract_desc desc = contract_desc(&requirement, 1);
    REQUIRE(tc_shader_set_contract(shader, &desc));
    tc_shader_mark_resource_layout_known(shader);

    termin::ShaderContractValidationOptions options{};
    options.debug_context = "contract-test";
    options.require_contract = true;
    CHECK(!termin::validate_shader_contract(shader, options));

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("shader contract validation rejects incompatible resource layout entry") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-contract-layout-incompatible");
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_binding binding = material_binding();
    binding.kind = TC_SHADER_RESOURCE_TEXTURE;
    tc_shader_set_resource_layout(shader, &binding, 1);

    tc_shader_resource_requirement requirement = material_requirement();
    tc_shader_contract_desc desc = contract_desc(&requirement, 1);
    REQUIRE(tc_shader_set_contract(shader, &desc));

    termin::ShaderContractValidationOptions options{};
    options.debug_context = "contract-test";
    options.require_contract = true;
    CHECK(!termin::validate_shader_contract(shader, options));

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}

TEST_CASE("shader contract validation still rejects a genuine stage mismatch") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-contract-stage-incompatible");
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_binding binding = material_binding();
    binding.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    tc_shader_set_resource_layout(shader, &binding, 1);

    tc_shader_resource_requirement requirement = material_requirement();
    requirement.stage_mask = TC_SHADER_STAGE_VERTEX | TC_SHADER_STAGE_FRAGMENT;
    tc_shader_contract_desc desc = contract_desc(&requirement, 1);
    desc.source_kind = TC_SHADER_CONTRACT_SOURCE_ASSEMBLED;
    REQUIRE(tc_shader_set_contract(shader, &desc));

    termin::ShaderContractValidationOptions options{};
    options.debug_context = "contract-test";
    options.require_contract = true;
    CHECK(!termin::validate_shader_contract(shader, options));

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}
