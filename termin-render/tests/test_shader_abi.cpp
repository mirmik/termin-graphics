#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/shader_abi.hpp>

#include <cstdio>
#include <string_view>

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

using namespace termin;

namespace {

tc_shader_resource_binding binding(
    const char* name,
    uint32_t kind,
    uint32_t scope)
{
    tc_shader_resource_binding result{};
    std::snprintf(result.name, sizeof(result.name), "%s", name);
    result.kind = kind;
    result.scope = scope;
    result.set = TC_SHADER_RESOURCE_SET_DEFAULT;
    result.binding = 0;
    result.stage_mask = TC_SHADER_STAGE_VERTEX;
    return result;
}

} // namespace

TEST_CASE("shader ABI exposes canonical resource contracts") {
    const ShaderAbiResourceDecl& draw =
        shader_abi_resource(ShaderAbiResourceId::DrawData);
    CHECK(draw.canonical_name == TC_SHADER_RESOURCE_DRAW_DATA);
    CHECK(draw.kind == TC_SHADER_RESOURCE_CONSTANT_BUFFER);
    CHECK(draw.scope == TC_SHADER_RESOURCE_SCOPE_DRAW);

    const ShaderAbiResourceDecl& lighting =
        shader_abi_resource(ShaderAbiResourceId::Lighting);
    CHECK(lighting.canonical_name == "lighting");
    CHECK(lighting.kind == TC_SHADER_RESOURCE_CONSTANT_BUFFER);
    CHECK(lighting.scope == TC_SHADER_RESOURCE_SCOPE_PASS);

    const ShaderAbiResourceDecl& shadows =
        shader_abi_resource(ShaderAbiResourceId::ShadowMaps);
    CHECK(shadows.canonical_name == "shadow_maps");
    CHECK(shadows.kind == TC_SHADER_RESOURCE_TEXTURE);
    CHECK(shadows.scope == TC_SHADER_RESOURCE_SCOPE_PASS);
}

TEST_CASE("shader ABI lookup accepts documented legacy aliases only") {
    const ShaderAbiResourceDecl* draw = find_shader_abi_resource("draw");
    REQUIRE(draw != nullptr);
    CHECK(draw->id == ShaderAbiResourceId::DrawData);
    CHECK(shader_abi_name_is_legacy_alias(*draw, "draw"));
    CHECK(!shader_abi_name_is_legacy_alias(*draw, "draw_data"));

    const ShaderAbiResourceDecl* lighting =
        find_shader_abi_resource("LightingBlock");
    REQUIRE(lighting != nullptr);
    CHECK(lighting->id == ShaderAbiResourceId::Lighting);
    CHECK(shader_abi_name_is_legacy_alias(*lighting, "LightingBlock"));

    CHECK(find_shader_abi_resource("lightingBlock") == nullptr);
    CHECK(find_shader_abi_resource("u_lighting") == nullptr);
}

TEST_CASE("shader ABI resource binding lookup validates kind and scope") {
    tc_shader_init();

    tc_shader_handle handle = tc_shader_create("shader-abi-layout");
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_shader_resource_binding draw =
        binding(TC_SHADER_RESOURCE_DRAW, TC_SHADER_RESOURCE_CONSTANT_BUFFER, TC_SHADER_RESOURCE_SCOPE_DRAW);
    tc_shader_set_resource_layout(shader, &draw, 1);

    const tc_shader_resource_binding* found =
        find_shader_abi_resource_binding(shader, ShaderAbiResourceId::DrawData);
    REQUIRE(found != nullptr);
    CHECK(std::string_view(found->name) == TC_SHADER_RESOURCE_DRAW);

    draw.kind = TC_SHADER_RESOURCE_TEXTURE;
    tc_shader_set_resource_layout(shader, &draw, 1);
    CHECK(find_shader_abi_resource_binding(shader, ShaderAbiResourceId::DrawData) == nullptr);

    tc_shader_destroy(handle);
    tc_shader_shutdown();
}
