#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/builtin_passes.hpp>
#include <termin/render/execute_context.hpp>
#include <termin/render/frame_graph_resource_registry.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_engine.hpp>
#include <termin/render/render_item_source.hpp>

#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

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
        g_error_log += message;
        g_error_log += '\n';
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

constexpr const char* kTestResourceType = "test_non_texture_resource";
int g_resource_create_count = 0;
int g_resource_destroy_count = 0;
int g_resource_preview_count = 0;
bool g_resource_producer_executed = false;
bool g_resource_alias_executed = false;

class TestFrameGraphResource final : public termin::FrameGraphResource {
public:
    int extent = 0;

    explicit TestFrameGraphResource(int extent_value)
        : extent(extent_value)
    {}

    ~TestFrameGraphResource() override {
        ++g_resource_destroy_count;
    }

    const char* resource_type() const override {
        return kTestResourceType;
    }
};

termin::FrameGraphResource* create_test_resource(
    const termin::ResourceSpec& spec)
{
    ++g_resource_create_count;
    return new TestFrameGraphResource(spec.size ? spec.size->first : 0);
}

termin::FrameGraphResourceSampledTexture preview_test_resource(
    const termin::FrameGraphResource& resource)
{
    CHECK(std::string(resource.resource_type()) == kTestResourceType);
    ++g_resource_preview_count;
    return {
        .texture = tgfx::TextureHandle{42},
        .kind = termin::FrameGraphResourceSampledTextureKind::Depth,
    };
}

class TestResourceProducer final : public termin::CxxFramePass {
public:
    TestResourceProducer() {
        pass_name_set("TestResourceProducer");
    }

    std::set<const char*> compute_writes() const override {
        return {"test_resource"};
    }

    std::vector<termin::ResourceSpec> get_resource_specs() const override {
        return {termin::ResourceSpec{
            "test_resource",
            kTestResourceType,
            std::pair<int, int>{17, 17},
        }};
    }

    void execute(termin::ExecuteContext& context) override {
        auto* resource =
            context.get_frame_graph_resource_as<TestFrameGraphResource>(
                "test_resource");
        REQUIRE(resource != nullptr);
        CHECK(resource->extent == 17);
        CHECK(context.tex2_reads.find("test_resource")
            == context.tex2_reads.end());
        const auto preview = context.tex2_depth_reads.find("test_resource");
        REQUIRE(preview != context.tex2_depth_reads.end());
        CHECK(preview->second == tgfx::TextureHandle{42});
        g_resource_producer_executed = true;
    }
};

class TestResourceAlias final : public termin::CxxFramePass {
public:
    TestResourceAlias() {
        pass_name_set("TestResourceAlias");
    }

    std::set<const char*> compute_reads() const override {
        return {"test_resource"};
    }

    std::set<const char*> compute_writes() const override {
        return {"test_resource_alias"};
    }

    std::vector<std::pair<std::string, std::string>>
    get_inplace_aliases() const override
    {
        return {{"test_resource", "test_resource_alias"}};
    }

    void execute(termin::ExecuteContext& context) override {
        auto* original = context.get_frame_graph_resource_as<TestFrameGraphResource>(
            "test_resource");
        auto* alias = context.get_frame_graph_resource_as<TestFrameGraphResource>(
            "test_resource_alias");
        REQUIRE(original != nullptr);
        CHECK(alias == original);
        g_resource_alias_executed = true;
    }
};

class UnknownResourceProducer final : public termin::CxxFramePass {
public:
    UnknownResourceProducer() {
        pass_name_set("UnknownResourceProducer");
    }

    std::set<const char*> compute_writes() const override {
        return {"unknown_resource"};
    }

    std::vector<termin::ResourceSpec> get_resource_specs() const override {
        return {termin::ResourceSpec{"unknown_resource", "unknown_resource_type"}};
    }

    void execute(termin::ExecuteContext&) override {
        FAIL("pass with an unknown resource kind must not execute");
    }
};

void publish_empty_snapshot(termin::RenderItemSnapshot& snapshot)
{
    InMemoryRenderItemSource source(0);
    REQUIRE(source.publish(snapshot, {}));
}

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

TEST_CASE("non-texture resource registry rejects duplicate registrations observably") {
    if (termin::has_frame_graph_resource_type(kTestResourceType)) {
        REQUIRE(termin::unregister_frame_graph_resource_type(kTestResourceType));
    }
    g_error_log.clear();
    tc_log_set_callback(capture_error);
    CHECK(termin::register_frame_graph_resource_type({
        .resource_type = kTestResourceType,
        .create = create_test_resource,
        .sampled_texture = preview_test_resource,
    }));
    CHECK(!termin::register_frame_graph_resource_type({
        .resource_type = kTestResourceType,
        .create = create_test_resource,
    }));
    tc_log_set_callback(nullptr);
    CHECK(g_error_log.find("already registered") != std::string::npos);
    CHECK(termin::unregister_frame_graph_resource_type(kTestResourceType));
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

TEST_CASE("generic execution allocates and binds registered non-texture resources once") {
    REQUIRE(termin::register_frame_graph_resource_type({
        .resource_type = kTestResourceType,
        .create = create_test_resource,
        .sampled_texture = preview_test_resource,
    }));
    g_resource_create_count = 0;
    g_resource_destroy_count = 0;
    g_resource_preview_count = 0;

    termin::RenderPipeline pipeline("generic-resource-execution-test");
    REQUIRE(pipeline.is_valid());
    auto* alias = new TestResourceAlias();
    auto* producer = new TestResourceProducer();
    pipeline.add_pass(alias->tc_pass_ptr());
    pipeline.add_pass(producer->tc_pass_ptr());

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "GenericResourceTarget";
    target.render_rect = {0, 0, 1, 1};
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name, termin::RenderExecutionTarget{
        .context = &target,
        .render_items = &snapshot,
    });

    termin::RenderEngine engine;
    for (int execution_index = 0; execution_index < 2; ++execution_index) {
        g_resource_producer_executed = false;
        g_resource_alias_executed = false;
        engine.execute_pipeline(execution);
        CHECK(g_resource_producer_executed);
        CHECK(g_resource_alias_executed);
    }
    CHECK(g_resource_create_count == 1);
    CHECK(g_resource_preview_count >= 2);
    CHECK(g_resource_destroy_count == 0);

    pipeline.destroy();
    CHECK(g_resource_destroy_count == 1);
    CHECK(termin::unregister_frame_graph_resource_type(kTestResourceType));
}

TEST_CASE("generic execution rejects an unknown serialized resource kind") {
    termin::RenderPipeline pipeline("unknown-resource-execution-test");
    REQUIRE(pipeline.is_valid());
    auto* producer = new UnknownResourceProducer();
    pipeline.add_pass(producer->tc_pass_ptr());

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "UnknownResourceTarget";
    target.render_rect = {0, 0, 1, 1};
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.targets.emplace(target.name, termin::RenderExecutionTarget{
        .context = &target,
        .render_items = &snapshot,
    });

    g_error_log.clear();
    tc_log_set_callback(capture_error);
    termin::RenderEngine engine;
    engine.execute_pipeline(execution);
    tc_log_set_callback(nullptr);
    CHECK(g_error_log.find("unknown_resource") != std::string::npos);
    CHECK(g_error_log.find("unknown_resource_type") != std::string::npos);
    pipeline.destroy();
}
