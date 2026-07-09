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
};

tc_pass* create_ownership_probe_pass(void*) {
    auto* pass = new OwnershipProbePass();
    pass->retain();
    return pass->tc_pass_ptr();
}

} // namespace

TEST_CASE("Pipeline consumes registry-created CxxFramePass owner reference") {
    g_probe_alive = 0;
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
    REQUIRE_EQ(created_cpp_pass->ref_count(), 1);
    REQUIRE_EQ(g_probe_alive, 1);

    tc_pipeline_add_pass_take(pipeline, created_pass);

    REQUIRE_EQ(tc_pipeline_pass_count(pipeline), 1u);
    tc_pass* stored_pass = tc_pipeline_get_pass_at(pipeline, 0);
    REQUIRE(stored_pass != nullptr);
    auto* stored_cpp_pass = termin::CxxFramePass::from_tc(stored_pass);
    REQUIRE(stored_cpp_pass != nullptr);
    CHECK_EQ(stored_cpp_pass->ref_count(), 1);
    CHECK_EQ(g_probe_alive, 1);

    tc_pipeline_destroy(pipeline);

    CHECK_EQ(g_probe_alive, 0);
    CHECK_EQ(g_probe_destroyed, 1);

    tc_pass_registry_unregister(kOwnershipProbePassType);
}
