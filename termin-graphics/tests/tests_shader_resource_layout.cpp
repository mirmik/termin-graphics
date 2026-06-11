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
