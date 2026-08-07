#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/builtin_passes.hpp>
#include <termin/render/execute_context.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_engine.hpp>

#include <string>

extern "C" {
#include <render/tc_pass.h>
#include <render/tc_pipeline.h>
#include <tcbase/tc_log.h>
}

namespace {

constexpr const char* kProbeType = "GenericRenderExecutionProbe";
bool g_executed = false;
const termin::RenderItemSnapshot* g_expected_snapshot = nullptr;
std::string g_error_log;

void capture_error(tc_log_level level, const char* message) {
    if (level >= TC_LOG_ERROR && message) {
        g_error_log = message;
    }
}

class GenericExecutionProbe final : public termin::CxxFramePass {
public:
    GenericExecutionProbe() {
        pass_name_set("GenericExecutionProbe");
        link_to_type_registry(kProbeType);
    }

    void execute(termin::ExecuteContext& context) override {
        CHECK(context.render_item_snapshot == g_expected_snapshot);
        CHECK(context.capabilities == nullptr);
        g_executed = true;
    }
};

void register_probe() {
    if (!tc_pass_registry_has("CxxFramePass")) {
        termin::register_builtin_render_pass_types();
    }
    tc_pass_registry_unregister(kProbeType);
    auto descriptor =
        termin::FramePassTypeDescriptorBuilder::native<GenericExecutionProbe>(
            kProbeType, "termin-render-test");
    REQUIRE(descriptor.commit());
}

TEST_CASE("generic pipeline rejects a missing published snapshot observably") {
    termin::RenderPipeline pipeline("generic-execution-missing-snapshot");
    REQUIRE(pipeline.is_valid());
    termin::RenderTargetContext target;
    target.name = "MissingSnapshotTarget";

    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.targets.emplace(target.name, termin::RenderExecutionTarget{
        .context = &target,
    });

    g_error_log.clear();
    tc_log_set_callback(capture_error);
    termin::RenderEngine engine;
    engine.execute_pipeline(execution);
    tc_log_set_callback(nullptr);

    CHECK(g_error_log.find("MissingSnapshotTarget") != std::string::npos);
    CHECK(g_error_log.find("RenderItemSnapshot") != std::string::npos);
    pipeline.destroy();
}

} // namespace

TEST_CASE("generic pipeline executes from a published snapshot without a scene") {
    register_probe();

    termin::RenderPipeline pipeline("generic-execution-test");
    REQUIRE(pipeline.is_valid());
    tc_pass* pass = tc_pass_registry_create(kProbeType);
    REQUIRE(pass != nullptr);
    pipeline.add_pass(pass);

    termin::RenderItemSnapshot snapshot;
    snapshot.begin_collection();
    snapshot.finish_collection({});
    REQUIRE(snapshot.valid());

    termin::RenderTargetContext target;
    target.name = "GenericTarget";
    target.render_rect = {0, 0, 1, 1};

    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name, termin::RenderExecutionTarget{
        .context = &target,
        .render_items = &snapshot,
        .capabilities = nullptr,
    });

    g_executed = false;
    g_expected_snapshot = &snapshot;
    termin::RenderEngine engine;
    engine.execute_pipeline(execution);
    CHECK(g_executed);

    pipeline.destroy();
    tc_pass_registry_unregister(kProbeType);
    g_expected_snapshot = nullptr;
}
