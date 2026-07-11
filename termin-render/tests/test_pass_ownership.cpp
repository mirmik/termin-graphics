#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/frame_pass.hpp>

extern "C" {
#include "render/tc_pass.h"
#include "render/tc_pipeline.h"
}

namespace {

constexpr const char* kOwnershipProbePassType = "OwnershipProbePass";
int g_probe_alive = 0;
int g_probe_lifecycle_destroyed = 0;
int g_probe_destroyed = 0;

class OwnershipProbePass : public termin::CxxFramePass {
public:
    OwnershipProbePass() {
        ++g_probe_alive;
        pass_name_set("OwnershipProbe");
        link_to_type_registry(kOwnershipProbePassType);
    }

    ~OwnershipProbePass() override {
        --g_probe_alive;
        ++g_probe_destroyed;
    }

    void destroy() override {
        ++g_probe_lifecycle_destroyed;
    }
};

tc_pass* create_ownership_probe_pass(void*) {
    auto* pass = new OwnershipProbePass();
    return pass->tc_pass_ptr();
}

} // namespace

TEST_CASE("Pipeline consumes registry-created CxxFramePass owner reference") {
    g_probe_alive = 0;
    g_probe_lifecycle_destroyed = 0;
    g_probe_destroyed = 0;

    tc_pass_registry_unregister(kOwnershipProbePassType);
    tc_pass_registry_register(
        kOwnershipProbePassType,
        create_ownership_probe_pass,
        nullptr,
        TC_NATIVE_PASS);

    tc_pipeline_handle pipeline = tc_pipeline_create("ownership-test");
    REQUIRE(tc_pipeline_handle_valid(pipeline));

    tc_pass* created_pass = tc_pass_registry_create(kOwnershipProbePassType);
    REQUIRE(created_pass != nullptr);

    auto* created_cpp_pass = termin::CxxFramePass::from_tc(created_pass);
    REQUIRE(created_cpp_pass != nullptr);
    REQUIRE_EQ(g_probe_alive, 1);

    REQUIRE(tc_pipeline_adopt_pass(pipeline, created_pass, created_pass->deleter));

    REQUIRE_EQ(tc_pipeline_pass_count(pipeline), 1u);
    tc_pass* stored_pass = tc_pipeline_get_pass_at(pipeline, 0);
    REQUIRE(stored_pass != nullptr);
    auto* stored_cpp_pass = termin::CxxFramePass::from_tc(stored_pass);
    REQUIRE(stored_cpp_pass != nullptr);
    CHECK_EQ(g_probe_alive, 1);

    tc_pipeline_destroy(pipeline);

    CHECK_EQ(g_probe_alive, 0);
    CHECK_EQ(g_probe_lifecycle_destroyed, 1);
    CHECK_EQ(g_probe_destroyed, 1);

    tc_pass_registry_unregister(kOwnershipProbePassType);
}

TEST_CASE("Pass adoption is atomic and rejects missing deleter or a second owner") {
    g_probe_alive = 0;
    g_probe_lifecycle_destroyed = 0;
    g_probe_destroyed = 0;

    tc_pass_registry_unregister(kOwnershipProbePassType);
    tc_pass_registry_register(
        kOwnershipProbePassType,
        create_ownership_probe_pass,
        nullptr,
        TC_NATIVE_PASS);

    const tc_pipeline_handle first = tc_pipeline_create("first-owner");
    const tc_pipeline_handle second = tc_pipeline_create("second-owner");
    tc_pass* pass = tc_pass_registry_create(kOwnershipProbePassType);
    REQUIRE(pass != nullptr);
    tc_pass_deleter deleter = pass->deleter;
    REQUIRE(deleter != nullptr);

    CHECK(!tc_pipeline_adopt_pass(first, pass, nullptr));
    CHECK(!tc_pipeline_handle_valid(pass->owner_pipeline));
    CHECK_EQ(tc_pipeline_pass_count(first), 0u);

    REQUIRE(tc_pipeline_adopt_pass(first, pass, deleter));
    CHECK(tc_pipeline_handle_eq(pass->owner_pipeline, first));
    CHECK(!tc_pipeline_adopt_pass(first, pass, deleter));
    CHECK(!tc_pipeline_adopt_pass(second, pass, deleter));
    CHECK_EQ(tc_pipeline_pass_count(first), 1u);
    CHECK_EQ(tc_pipeline_pass_count(second), 0u);
    CHECK_EQ(g_probe_alive, 1);

    tc_pipeline_destroy(second);
    tc_pipeline_destroy(first);
    CHECK_EQ(g_probe_lifecycle_destroyed, 1);
    CHECK_EQ(g_probe_destroyed, 1);
    tc_pass_registry_unregister(kOwnershipProbePassType);
}

TEST_CASE("Moving an owned pass preserves its single ownership record") {
    g_probe_alive = 0;
    g_probe_lifecycle_destroyed = 0;
    g_probe_destroyed = 0;
    tc_pass_registry_unregister(kOwnershipProbePassType);
    tc_pass_registry_register(
        kOwnershipProbePassType,
        create_ownership_probe_pass,
        nullptr,
        TC_NATIVE_PASS);

    const tc_pipeline_handle pipeline = tc_pipeline_create("move-owner");
    tc_pass* first = tc_pass_registry_create(kOwnershipProbePassType);
    tc_pass* second = tc_pass_registry_create(kOwnershipProbePassType);
    tc_pass* third = tc_pass_registry_create(kOwnershipProbePassType);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(third != nullptr);
    tc_pass_set_name(first, "first");
    tc_pass_set_name(second, "second");
    tc_pass_set_name(third, "third");
    REQUIRE(tc_pipeline_adopt_pass(pipeline, first, first->deleter));
    REQUIRE(tc_pipeline_adopt_pass(pipeline, second, second->deleter));
    REQUIRE(tc_pipeline_adopt_pass(pipeline, third, third->deleter));

    REQUIRE(tc_pipeline_move_pass_before(pipeline, first, nullptr));
    CHECK_EQ(std::string(tc_pipeline_get_pass_at(pipeline, 0)->pass_name), "second");
    CHECK_EQ(std::string(tc_pipeline_get_pass_at(pipeline, 1)->pass_name), "third");
    CHECK_EQ(std::string(tc_pipeline_get_pass_at(pipeline, 2)->pass_name), "first");
    CHECK_EQ(g_probe_lifecycle_destroyed, 0);
    CHECK_EQ(g_probe_destroyed, 0);

    REQUIRE(tc_pipeline_move_pass_before(pipeline, first, second));
    CHECK_EQ(std::string(tc_pipeline_get_pass_at(pipeline, 0)->pass_name), "first");
    CHECK_EQ(std::string(tc_pipeline_get_pass_at(pipeline, 1)->pass_name), "second");
    CHECK_EQ(std::string(tc_pipeline_get_pass_at(pipeline, 2)->pass_name), "third");
    CHECK_EQ(g_probe_alive, 3);

    tc_pipeline_destroy(pipeline);
    CHECK_EQ(g_probe_lifecycle_destroyed, 3);
    CHECK_EQ(g_probe_destroyed, 3);
    tc_pass_registry_unregister(kOwnershipProbePassType);
}
