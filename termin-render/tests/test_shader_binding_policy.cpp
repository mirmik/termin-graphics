#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/shader_binding_policy.hpp>

extern "C" {
#include <tgfx/resources/tc_shader_registry.h>
}

TEST_CASE("shader binding policy treats Slang layout as layout-only") {
    tc_shader_init();

    tc_shader_handle glsl_handle =
        tc_shader_create("test-binding-policy-glsl-layout");
    tc_shader_handle slang_handle =
        tc_shader_create("test-binding-policy-slang-layout");
    tc_shader_handle slang_legacy_handle =
        tc_shader_create("test-binding-policy-slang-legacy");
    REQUIRE(!tc_shader_handle_is_invalid(glsl_handle));
    REQUIRE(!tc_shader_handle_is_invalid(slang_handle));
    REQUIRE(!tc_shader_handle_is_invalid(slang_legacy_handle));

    tc_shader* glsl = tc_shader_get(glsl_handle);
    tc_shader* slang = tc_shader_get(slang_handle);
    tc_shader* slang_legacy = tc_shader_get(slang_legacy_handle);
    REQUIRE(glsl != nullptr);
    REQUIRE(slang != nullptr);
    REQUIRE(slang_legacy != nullptr);

    CHECK(tc_shader_get_language(glsl) == TC_SHADER_LANGUAGE_GLSL);
    REQUIRE(tc_shader_set_language(slang, TC_SHADER_LANGUAGE_SLANG));
    REQUIRE(tc_shader_set_language(slang_legacy, TC_SHADER_LANGUAGE_SLANG));

    tc_shader_mark_resource_layout_known(glsl);
    tc_shader_mark_resource_layout_known(slang);

    CHECK(termin::shader_has_layout_metadata(glsl));
    CHECK(!termin::shader_uses_layout_only_bindings(glsl));
    CHECK(termin::shader_allows_legacy_resource_fallback(glsl));

    CHECK(termin::shader_has_layout_metadata(slang));
    CHECK(termin::shader_uses_layout_only_bindings(slang));
    CHECK(!termin::shader_allows_legacy_resource_fallback(slang));

    CHECK(!termin::shader_has_layout_metadata(slang_legacy));
    CHECK(!termin::shader_uses_layout_only_bindings(slang_legacy));
    CHECK(termin::shader_allows_legacy_resource_fallback(slang_legacy));

    tc_shader_destroy(glsl_handle);
    tc_shader_destroy(slang_handle);
    tc_shader_destroy(slang_legacy_handle);
    tc_shader_shutdown();
}
