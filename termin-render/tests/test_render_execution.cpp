#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/builtin_passes.hpp>
#include <termin/render/execute_context.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_engine.hpp>
#include <termin/render/render_item_source.hpp>

#include <cstddef>
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
size_t g_expected_item_count = 0;
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
        REQUIRE(context.render_item_snapshot != nullptr);
        CHECK(context.render_item_snapshot->item_count() == g_expected_item_count);
        CHECK(context.capabilities == nullptr);
        g_executed = true;
    }
};

class InMemoryRenderItemSource final : public termin::RenderItemSource {
private:
    size_t item_count_ = 0;

protected:
    const char* source_name() const noexcept override {
        return "InMemoryRenderItemSource";
    }

    bool collect_items(
        const termin::RenderItemSourceRequest& request,
        termin::RenderItemCollection& output,
        termin::RenderItemSnapshotCounters& counters) override
    {
        last_view = request.view;
        last_layer_mask = request.layer_mask;
        last_render_category_mask = request.render_category_mask;
        for (size_t item_index = 0; item_index < item_count_; ++item_index) {
            tc_render_item item{};
            item.kind = TC_RENDER_ITEM_KIND_MESH;
            item.source.domain_id = 77;
            item.source.object_id = item_index + 1;
            output.items.push_back(item);
        }
        counters.source_traversals = 1;
        counters.producers = 1;
        return true;
    }

public:
    const termin::RenderViewState* last_view = nullptr;
    uint64_t last_layer_mask = 0;
    uint64_t last_render_category_mask = 0;

    explicit InMemoryRenderItemSource(size_t item_count)
        : item_count_(item_count)
    {}

    void set_item_count(size_t item_count) {
        item_count_ = item_count;
    }
};

class FailingRenderItemSource final : public termin::RenderItemSource {
protected:
    const char* source_name() const noexcept override {
        return "FailingRenderItemSource";
    }

    bool collect_items(
        const termin::RenderItemSourceRequest&,
        termin::RenderItemCollection& output,
        termin::RenderItemSnapshotCounters&) override
    {
        output.items.emplace_back();
        return false;
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

TEST_CASE("failed item source publication invalidates partial output and logs") {
    termin::RenderItemSnapshot snapshot;
    FailingRenderItemSource source;
    termin::RenderItemSourceRequest request{};
    request.debug_name = "FailureTarget";

    g_error_log.clear();
    tc_log_set_callback(capture_error);
    CHECK(!source.publish(snapshot, request));
    tc_log_set_callback(nullptr);

    CHECK(!snapshot.valid());
    CHECK(snapshot.item_count() == 0);
    CHECK(g_error_log.find("FailingRenderItemSource") != std::string::npos);
    CHECK(g_error_log.find("FailureTarget") != std::string::npos);
}

} // namespace

TEST_CASE("generic pipeline executes empty and populated non-scene sources") {
    register_probe();

    termin::RenderPipeline pipeline("generic-execution-test");
    REQUIRE(pipeline.is_valid());
    tc_pass* pass = tc_pass_registry_create(kProbeType);
    REQUIRE(pass != nullptr);
    pipeline.add_pass(pass);

    termin::RenderViewState view;
    termin::RenderItemSourceRequest source_request{};
    source_request.view = &view;
    source_request.layer_mask = 0x12;
    source_request.render_category_mask = 0x34;
    source_request.debug_name = "GenericTarget";

    InMemoryRenderItemSource source(0);
    termin::RenderItemSnapshot snapshot;
    REQUIRE(source.publish(snapshot, source_request));
    REQUIRE(snapshot.valid());
    CHECK(snapshot.item_count() == 0);
    CHECK(snapshot.counters().source_traversals == 1);
    CHECK(snapshot.counters().producers == 1);
    CHECK(source.last_view == &view);
    CHECK(source.last_layer_mask == 0x12);
    CHECK(source.last_render_category_mask == 0x34);

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
    g_expected_item_count = 0;
    termin::RenderEngine engine;
    engine.execute_pipeline(execution);
    CHECK(g_executed);

    source.set_item_count(2);
    REQUIRE(source.publish(snapshot, source_request));
    REQUIRE(snapshot.valid());
    CHECK(snapshot.item_count() == 2);
    CHECK(snapshot.item(0)->source.domain_id == 77);
    CHECK(snapshot.item(1)->source.object_id == 2);

    g_executed = false;
    g_expected_item_count = 2;
    engine.execute_pipeline(execution);
    CHECK(g_executed);

    pipeline.destroy();
    tc_pass_registry_unregister(kProbeType);
    g_expected_snapshot = nullptr;
    g_expected_item_count = 0;
}
