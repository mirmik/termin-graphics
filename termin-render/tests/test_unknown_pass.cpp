#include "guard_main.h"

GUARD_TEST_MAIN();

#include <tc_inspect_cpp.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/unknown_pass.hpp>
#include <termin/render/unknown_pass_ops.hpp>

extern "C" {
#include <render/tc_pass.h>
#include <render/tc_pipeline.h>
}

namespace {

constexpr const char* kProbeType = "UnknownPassProbe";

class UnknownPassProbe final : public termin::CxxFramePass {
public:
    int exposure = 0;

    UnknownPassProbe() {
        link_to_type_registry(kProbeType);
    }

    std::set<const char*> compute_reads() const override { return {"scene_color"}; }
    std::set<const char*> compute_writes() const override { return {"post_color"}; }
    std::vector<std::pair<std::string, std::string>> get_inplace_aliases() const override {
        return {{"scene_color", "post_color"}};
    }
    std::vector<termin::ResourceSpec> get_resource_specs() const override {
        return {termin::ResourceSpec("post_color", "color_texture")};
    }
    std::vector<std::string> get_internal_symbols() const override {
        return {"histogram"};
    }
};

tc_pass* create_probe(void*) {
    auto* pass = new UnknownPassProbe();
    return pass->tc_pass_ptr();
}

void register_probe() {
    tc_pass_registry_register(kProbeType, create_probe, nullptr, TC_NATIVE_PASS);
    if (!tc::InspectRegistry::instance().find_field(kProbeType, "exposure")) {
        tc::InspectRegistry::instance().add<UnknownPassProbe, int>(
            kProbeType,
            &UnknownPassProbe::exposure,
            "exposure",
            "Exposure",
            "int"
        );
    }
}

} // namespace

TEST_CASE("UnknownPass preserves pipeline slot payload and graph contract") {
    tc::init_cpp_inspect_vtable();
    tc::register_builtin_cpp_kinds();
    register_probe();

    const tc_pipeline_handle pipeline = tc_pipeline_create("unknown-pass-test");
    REQUIRE(tc_pipeline_handle_valid(pipeline));
    tc_pass* raw = tc_pass_registry_create(kProbeType);
    REQUIRE(raw != nullptr);
    auto* probe = dynamic_cast<UnknownPassProbe*>(termin::CxxFramePass::from_tc(raw));
    REQUIRE(probe != nullptr);
    probe->exposure = 17;
    tc_pass_set_name(raw, "post");
    tc_pass_set_enabled(raw, false);
    tc_pass_set_passthrough(raw, true);
    tc_pass_set_viewport_name(raw, "main");
    REQUIRE(tc_pipeline_adopt_pass(pipeline, raw, raw->deleter));

    const termin::UnknownPassStats degraded =
        termin::degrade_passes_to_unknown({kProbeType});
    REQUIRE_EQ(degraded.degraded, 1u);
    REQUIRE_EQ(degraded.failed, 0u);
    REQUIRE_EQ(tc_pipeline_pass_count(pipeline), 1u);
    CHECK_EQ(tc_pass_registry_instance_count(kProbeType), 0u);

    tc_pass* placeholder_raw = tc_pipeline_get_pass_at(pipeline, 0);
    REQUIRE(placeholder_raw != nullptr);
    CHECK_EQ(std::string(tc_pass_type_name(placeholder_raw)), "UnknownPass");
    auto* placeholder = dynamic_cast<termin::UnknownPass*>(
        termin::CxxFramePass::from_tc(placeholder_raw));
    REQUIRE(placeholder != nullptr);
    CHECK_EQ(placeholder->original_type, kProbeType);
    REQUIRE_EQ(placeholder->original_reads.size(), 1u);
    CHECK_EQ(placeholder->original_reads[0], "scene_color");
    REQUIRE_EQ(placeholder->original_writes.size(), 1u);
    CHECK_EQ(placeholder->original_writes[0], "post_color");
    REQUIRE_EQ(placeholder->original_inplace_aliases.size(), 1u);
    CHECK_EQ(placeholder->original_inplace_aliases[0].first, "scene_color");
    REQUIRE_EQ(placeholder->original_resource_specs.size(), 1u);
    CHECK_EQ(placeholder->original_resource_specs[0].resource, "post_color");
    REQUIRE_EQ(placeholder->original_internal_symbols.size(), 1u);
    CHECK_EQ(placeholder->original_internal_symbols[0], "histogram");
    tc_value* exposure = tc_value_dict_get(&placeholder->original_data, "exposure");
    REQUIRE(exposure != nullptr);
    CHECK_EQ(exposure->data.i, 17);
    CHECK_EQ(std::string(placeholder_raw->pass_name), "post");
    CHECK(!placeholder_raw->enabled);
    CHECK(placeholder_raw->passthrough);
    CHECK_EQ(std::string(placeholder_raw->viewport_name), "main");

    tc_pass_registry_unregister(kProbeType);
    const termin::UnknownPassStats unavailable = termin::upgrade_unknown_passes({kProbeType});
    CHECK_EQ(unavailable.upgraded, 0u);
    CHECK_EQ(unavailable.skipped, 1u);
    CHECK_EQ(tc_pipeline_get_pass_at(pipeline, 0), placeholder_raw);

    register_probe();
    const termin::UnknownPassStats upgraded = termin::upgrade_unknown_passes({kProbeType});
    REQUIRE_EQ(upgraded.upgraded, 1u);
    REQUIRE_EQ(upgraded.failed, 0u);
    tc_pass* restored_raw = tc_pipeline_get_pass_at(pipeline, 0);
    REQUIRE(restored_raw != nullptr);
    CHECK_EQ(std::string(tc_pass_type_name(restored_raw)), kProbeType);
    auto* restored = dynamic_cast<UnknownPassProbe*>(
        termin::CxxFramePass::from_tc(restored_raw));
    REQUIRE(restored != nullptr);
    CHECK_EQ(restored->exposure, 17);
    CHECK_EQ(std::string(restored_raw->pass_name), "post");
    CHECK(!restored_raw->enabled);
    CHECK(restored_raw->passthrough);
    CHECK_EQ(std::string(restored_raw->viewport_name), "main");

    tc_pipeline_destroy(pipeline);
    tc_pass_registry_unregister(kProbeType);
    tc::InspectRegistry::instance().unregister_type(kProbeType);
}

TEST_CASE("UnknownPass keeps placeholder on schema drift") {
    tc::init_cpp_inspect_vtable();
    tc::register_builtin_cpp_kinds();
    register_probe();

    const tc_pipeline_handle pipeline = tc_pipeline_create("unknown-pass-schema-drift");
    tc_pass* raw = tc_pass_registry_create(kProbeType);
    REQUIRE(raw != nullptr);
    auto* probe = dynamic_cast<UnknownPassProbe*>(termin::CxxFramePass::from_tc(raw));
    REQUIRE(probe != nullptr);
    probe->exposure = 31;
    REQUIRE(tc_pipeline_adopt_pass(pipeline, raw, raw->deleter));
    REQUIRE_EQ(termin::degrade_passes_to_unknown({kProbeType}).degraded, 1u);

    tc_pass* placeholder_raw = tc_pipeline_get_pass_at(pipeline, 0);
    auto* placeholder = dynamic_cast<termin::UnknownPass*>(
        termin::CxxFramePass::from_tc(placeholder_raw));
    REQUIRE(placeholder != nullptr);
    tc_value_dict_set(&placeholder->original_data, "removed_field", tc_value_int(9));
    tc_value payload_copy = tc_value_copy(&placeholder->original_data);

    const termin::UnknownPassStats upgraded =
        termin::upgrade_unknown_passes({kProbeType});
    CHECK_EQ(upgraded.upgraded, 0u);
    CHECK_EQ(upgraded.failed, 1u);
    CHECK_EQ(tc_pipeline_get_pass_at(pipeline, 0), placeholder_raw);
    CHECK_EQ(std::string(tc_pass_type_name(placeholder_raw)), "UnknownPass");
    CHECK(tc_value_equals(&placeholder->original_data, &payload_copy));

    tc_value_free(&payload_copy);
    tc_pipeline_destroy(pipeline);
    tc_pass_registry_unregister(kProbeType);
    tc::InspectRegistry::instance().unregister_type(kProbeType);
}

TEST_CASE("UnknownPass preparation failures leave every pipeline unchanged") {
    tc::init_cpp_inspect_vtable();
    tc::register_builtin_cpp_kinds();
    register_probe();

    for (size_t failure_index = 0; failure_index < 3; ++failure_index) {
        const tc_pipeline_handle first_pipeline = tc_pipeline_create("prepare-first");
        const tc_pipeline_handle second_pipeline = tc_pipeline_create("prepare-second");
        tc_pass* first = tc_pass_registry_create(kProbeType);
        tc_pass* middle = tc_pass_registry_create(kProbeType);
        tc_pass* last = tc_pass_registry_create(kProbeType);
        REQUIRE(first != nullptr);
        REQUIRE(middle != nullptr);
        REQUIRE(last != nullptr);
        REQUIRE(tc_pipeline_adopt_pass(first_pipeline, first, first->deleter));
        REQUIRE(tc_pipeline_adopt_pass(first_pipeline, middle, middle->deleter));
        REQUIRE(tc_pipeline_adopt_pass(second_pipeline, last, last->deleter));

        size_t serialize_call = 0;
        termin::UnknownPassPreparationHooks hooks;
        hooks.serialize = [&](void* object, const char* type_name) {
            if (serialize_call++ == failure_index) return tc_value_nil();
            return tc_inspect_serialize(object, type_name);
        };

        termin::UnknownPassDegradationPlan plan;
        std::string error;
        CHECK(!termin::prepare_passes_to_unknown(
            {kProbeType}, plan, &error, hooks));
        CHECK_EQ(tc_pipeline_get_pass_at(first_pipeline, 0), first);
        CHECK_EQ(tc_pipeline_get_pass_at(first_pipeline, 1), middle);
        CHECK_EQ(tc_pipeline_get_pass_at(second_pipeline, 0), last);
        CHECK_EQ(tc_pass_registry_instance_count(kProbeType), 3u);

        tc_pipeline_destroy(first_pipeline);
        tc_pipeline_destroy(second_pipeline);
    }

    tc_pass_registry_unregister(kProbeType);
    tc::InspectRegistry::instance().unregister_type(kProbeType);
}

TEST_CASE("UnknownPass prepared plan rejects stale pipeline identity") {
    tc::init_cpp_inspect_vtable();
    tc::register_builtin_cpp_kinds();
    register_probe();

    const tc_pipeline_handle pipeline = tc_pipeline_create("prepare-stale");
    tc_pass* first = tc_pass_registry_create(kProbeType);
    tc_pass* second = tc_pass_registry_create(kProbeType);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(tc_pipeline_adopt_pass(pipeline, first, first->deleter));
    REQUIRE(tc_pipeline_adopt_pass(pipeline, second, second->deleter));

    termin::UnknownPassDegradationPlan plan;
    std::string error;
    REQUIRE(termin::prepare_passes_to_unknown({kProbeType}, plan, &error));
    REQUIRE_EQ(plan.size(), 2u);
    REQUIRE(tc_pipeline_move_pass_before(pipeline, second, first));

    CHECK(!plan.commit(&error));
    CHECK_EQ(tc_pipeline_get_pass_at(pipeline, 0), second);
    CHECK_EQ(tc_pipeline_get_pass_at(pipeline, 1), first);
    CHECK_EQ(tc_pass_registry_instance_count(kProbeType), 2u);

    tc_pipeline_destroy(pipeline);
    tc_pass_registry_unregister(kProbeType);
    tc::InspectRegistry::instance().unregister_type(kProbeType);
}

TEST_CASE("UnknownPass preparation rejects external live instances") {
    tc::init_cpp_inspect_vtable();
    tc::register_builtin_cpp_kinds();
    register_probe();

    const tc_pipeline_handle pipeline = tc_pipeline_create("prepare-external");
    tc_pass* attached = tc_pass_registry_create(kProbeType);
    tc_pass* external = tc_pass_registry_create(kProbeType);
    REQUIRE(attached != nullptr);
    REQUIRE(external != nullptr);
    REQUIRE(tc_pipeline_adopt_pass(pipeline, attached, attached->deleter));

    termin::UnknownPassDegradationPlan plan;
    std::string error;
    CHECK(!termin::prepare_passes_to_unknown({kProbeType}, plan, &error));
    CHECK_EQ(tc_pipeline_get_pass_at(pipeline, 0), attached);
    CHECK_EQ(tc_pass_registry_instance_count(kProbeType), 2u);

    tc_pass_delete_unowned(external);
    tc_pipeline_destroy(pipeline);
    tc_pass_registry_unregister(kProbeType);
    tc::InspectRegistry::instance().unregister_type(kProbeType);
}

TEST_CASE("UnknownPass registration survives registry rebootstrap") {
    tc_pass_registry_cleanup();
    CHECK(!tc_pass_registry_has("UnknownPass"));
    termin::ensure_unknown_pass_registered();
    REQUIRE(tc_pass_registry_has("UnknownPass"));
    tc_pass* placeholder = tc_pass_registry_create("UnknownPass");
    REQUIRE(placeholder != nullptr);
    CHECK_EQ(std::string(tc_pass_type_name(placeholder)), "UnknownPass");
    tc_pass_delete_unowned(placeholder);
}
