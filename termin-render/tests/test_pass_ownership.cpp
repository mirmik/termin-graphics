#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/frame_pass.hpp>
#include <termin/render/builtin_passes.hpp>

extern "C" {
#include "render/tc_pass.h"
#include "render/tc_pipeline.h"
#include "tc_pipeline_test_hooks.h"
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

void register_ownership_probe_pass() {
    if (!tc_pass_registry_has("CxxFramePass")) {
        termin::register_builtin_render_pass_types();
    }
    auto descriptor = termin::FramePassTypeDescriptorBuilder::native<OwnershipProbePass>(
        kOwnershipProbePassType, "termin-render-test");
    REQUIRE(descriptor.commit());
}

} // namespace

TEST_CASE("Pipeline consumes registry-created CxxFramePass owner reference") {
    g_probe_alive = 0;
    g_probe_lifecycle_destroyed = 0;
    g_probe_destroyed = 0;

    tc_pass_registry_unregister(kOwnershipProbePassType);
    register_ownership_probe_pass();

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
    register_ownership_probe_pass();

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
    register_ownership_probe_pass();

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

TEST_CASE("Pipeline shutdown destroys every live slot through the normal teardown path") {
    g_probe_alive = 0;
    g_probe_lifecycle_destroyed = 0;
    g_probe_destroyed = 0;
    int cache_destroyed = 0;

    tc_pass_registry_unregister(kOwnershipProbePassType);
    register_ownership_probe_pass();

    const tc_pipeline_handle pipeline = tc_pipeline_create("shutdown-owner");
    REQUIRE(tc_pipeline_handle_valid(pipeline));
    tc_pass* pass = tc_pass_registry_create(kOwnershipProbePassType);
    REQUIRE(pass != nullptr);
    REQUIRE(tc_pipeline_adopt_pass(pipeline, pass, pass->deleter));
    tc_pipeline_set_render_cache(
        pipeline,
        &cache_destroyed,
        [](void* value) { ++*static_cast<int*>(value); });
    REQUIRE(tc_pipeline_get_frame_graph(pipeline) != nullptr);

    tc_pipeline_pool_shutdown();

    CHECK_EQ(cache_destroyed, 1);
    CHECK_EQ(g_probe_alive, 0);
    CHECK_EQ(g_probe_lifecycle_destroyed, 1);
    CHECK_EQ(g_probe_destroyed, 1);
    tc_pass_registry_unregister(kOwnershipProbePassType);
}

TEST_CASE("Pipeline pool initialization is atomic across allocation failures") {
    tc_pipeline_pool_shutdown();
    for (size_t successful_allocations = 0; successful_allocations < 5;
         ++successful_allocations) {
        tc_pipeline_test_fail_storage_allocation_after(successful_allocations);
        const tc_pipeline_handle failed = tc_pipeline_create("must-not-publish");
        CHECK(!tc_pipeline_handle_valid(failed));
        CHECK_EQ(tc_pipeline_pool_count(), 0u);
        tc_pipeline_pool_shutdown();
    }

    tc_pipeline_test_reset_storage_allocator();
    const tc_pipeline_handle recovered = tc_pipeline_create("recovered");
    REQUIRE(tc_pipeline_handle_valid(recovered));
    tc_pipeline_destroy(recovered);
}

TEST_CASE("Failed pipeline pool growth preserves every existing handle") {
    tc_pipeline_pool_shutdown();
    tc_pipeline_test_reset_storage_allocator();

    tc_pipeline_handle handles[16];
    for (size_t i = 0; i < 16; ++i) {
        handles[i] = tc_pipeline_create("growth-preservation");
        REQUIRE(tc_pipeline_handle_valid(handles[i]));
    }

    for (size_t successful_allocations = 0; successful_allocations < 4;
         ++successful_allocations) {
        tc_pipeline_test_fail_storage_allocation_after(successful_allocations);
        const tc_pipeline_handle failed = tc_pipeline_create("failed-growth");
        CHECK(!tc_pipeline_handle_valid(failed));
        CHECK_EQ(tc_pipeline_pool_count(), 16u);
        for (const tc_pipeline_handle handle : handles) {
            CHECK(tc_pipeline_pool_alive(handle));
        }
    }

    tc_pipeline_test_reset_storage_allocator();
    const tc_pipeline_handle grown = tc_pipeline_create("successful-growth");
    REQUIRE(tc_pipeline_handle_valid(grown));
    tc_pipeline_destroy(grown);
    for (const tc_pipeline_handle handle : handles) {
        tc_pipeline_destroy(handle);
    }
    CHECK_EQ(tc_pipeline_pool_count(), 0u);
}
