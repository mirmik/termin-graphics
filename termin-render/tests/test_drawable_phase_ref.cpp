#include "guard_main.h"

GUARD_TEST_MAIN();

#include <cstdio>
#include <cstring>

#include <termin/render/drawable.hpp>

extern "C" {
#include <tgfx/resources/tc_material_registry.h>
#include <tgfx/resources/tc_shader_registry.h>
}

TEST_CASE("GeometryDrawCall resolves material phase after registry growth") {
    tc_shader_init();
    tc_material_init();

    tc_material_handle material = tc_material_create(
        "test-stable-phase-material",
        "test-stable-phase-material");
    REQUIRE(tc_material_is_valid(material));

    tc_material* raw_material = tc_material_get(material);
    REQUIRE(raw_material != nullptr);

    tc_material_phase* phase = tc_material_add_phase(
        raw_material,
        tc_shader_handle_invalid(),
        "transparent",
        17);
    REQUIRE(phase != nullptr);

    termin::GeometryDrawCall draw(phase, 42);
    CHECK(draw.has_stable_phase_ref());
    CHECK_EQ(draw.geometry_id, 42);

    for (int i = 0; i < 256; ++i) {
        char uuid[64];
        char name[64];
        std::snprintf(uuid, sizeof(uuid), "test-material-growth-%03d", i);
        std::snprintf(name, sizeof(name), "test-material-growth-%03d", i);
        tc_material_create(uuid, name);
    }

    tc_material_phase* resolved = draw.resolve_phase();
    REQUIRE(resolved != nullptr);
    CHECK_EQ(resolved->priority, 17);
    CHECK_EQ(std::strcmp(resolved->phase_mark, "transparent"), 0);

    tc_material_shutdown();
    tc_shader_shutdown();
}
