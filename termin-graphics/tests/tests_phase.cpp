#include "guard_main.h"

#include <tgfx/resources/tc_phase.h>

#include <cstring>

TEST_CASE("builtin phases have stable low bits")
{
    CHECK_EQ(tc_phase_find("opaque"), TC_PHASE_OPAQUE);
    CHECK_EQ(tc_phase_find("transparent"), TC_PHASE_TRANSPARENT);
    CHECK_EQ(tc_phase_find("normal"), TC_PHASE_NORMAL);
    CHECK_EQ(tc_phase_find("depth"), TC_PHASE_DEPTH);
    CHECK_EQ(tc_phase_find("id"), TC_PHASE_ID);
    CHECK_EQ(tc_phase_find("shadow"), TC_PHASE_SHADOW);
    CHECK(tc_phase_find("pick") == TC_PHASE_NONE);
}

TEST_CASE("project phase registration is stable until reset")
{
    tc_phase_clear_project_registry();
    REQUIRE(tc_phase_set_project_name(0, "gameplay_overlay"));
    const tc_phase_mask first = tc_phase_find("gameplay_overlay");
    CHECK_EQ(first, UINT64_C(1) << TC_PHASE_INDEX_USER_FIRST);
    REQUIRE(tc_phase_name(first));
    CHECK(std::strcmp(tc_phase_name(first), "gameplay_overlay") == 0);

    tc_phase_clear_project_registry();
    CHECK_EQ(tc_phase_find("gameplay_overlay"), TC_PHASE_NONE);
}

TEST_CASE("project phase registry rejects ambiguous assignments")
{
    tc_phase_clear_project_registry();
    CHECK_FALSE(tc_phase_set_project_name(TC_PHASE_PROJECT_CAPACITY, "outside"));
    CHECK_FALSE(tc_phase_set_project_name(0, "opaque"));
    REQUIRE(tc_phase_set_project_name(1, "overlay"));
    CHECK_FALSE(tc_phase_set_project_name(2, "overlay"));
}
