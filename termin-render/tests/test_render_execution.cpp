#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/builtin_passes.hpp>
#include <termin/render/execute_context.hpp>
#include <termin/render/frame_graph_resource_registry.hpp>
#include <termin/render/frame_pass.hpp>
#include <termin/render/render_engine.hpp>
#include <termin/render/render_item_source.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>

#include <cstddef>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
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

        bool collect_items(const termin::RenderItemSourceRequest& request,
                           termin::RenderItemCollection& output,
                           termin::RenderItemSnapshotCounters& counters) override {
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
            : item_count_(item_count) {}

        void set_item_count(size_t item_count) {
            item_count_ = item_count;
        }
    };

    class FailingRenderItemSource final : public termin::RenderItemSource {
    protected:
        const char* source_name() const noexcept override {
            return "FailingRenderItemSource";
        }

        bool collect_items(const termin::RenderItemSourceRequest&,
                           termin::RenderItemCollection& output,
                           termin::RenderItemSnapshotCounters&) override {
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
    bool g_external_alias_producer_executed = false;
    bool g_external_alias_consumer_executed = false;
    int g_raster_probe_execute_count = 0;
    int g_raster_probe_record_count = 0;
    int g_resolve_probe_execute_count = 0;

    struct RecordedRenderScope {
        tgfx::RenderPassDesc pass;
        uint32_t view_count = 1;
    };

    struct ExecutionRecordingState {
        std::vector<RecordedRenderScope> scopes;
        std::vector<std::pair<tgfx::TextureHandle, tgfx::TextureHandle>> texture_copies;
        uint32_t framebuffer_local_barriers = 0;
    };

    class ExecutionRecordingCommandList final : public tgfx::ICommandList {
    public:
        explicit ExecutionRecordingCommandList(ExecutionRecordingState& state)
            : state_(state) {}

        void begin() override {}
        void end() override {}
        void begin_render_pass(const tgfx::RenderPassDesc& pass) override {
            state_.scopes.push_back({pass, 1});
        }
        void begin_multiview_render_pass(const tgfx::MultiviewRenderPassDesc& pass) override {
            tgfx::RenderPassDesc base;
            base.colors = pass.colors;
            base.depth = pass.depth;
            base.has_depth = pass.has_depth;
            state_.scopes.push_back({std::move(base), pass.view_count});
        }
        void end_render_pass() override {}
        void framebuffer_local_barrier() override {
            ++state_.framebuffer_local_barriers;
        }
        void bind_pipeline(tgfx::PipelineHandle) override {}
        void bind_resource_set(tgfx::ResourceSetHandle,
                               uint32_t = 0,
                               const uint32_t* = nullptr,
                               uint32_t = 0) override {}
        void set_push_constants(const void*, uint32_t) override {}
        void bind_vertex_buffer(uint32_t, tgfx::BufferHandle, uint64_t = 0) override {}
        void bind_index_buffer(tgfx::BufferHandle, tgfx::IndexType, uint64_t = 0) override {}
        void draw(uint32_t, uint32_t = 0) override {}
        void draw_instanced(uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}
        void draw_indexed(uint32_t, uint32_t = 0, int32_t = 0) override {}
        void draw_indexed_instanced(uint32_t, uint32_t, uint32_t = 0, int32_t = 0, uint32_t = 0) override {}
        void dispatch(uint32_t, uint32_t, uint32_t) override {}
        void copy_buffer(tgfx::BufferHandle, tgfx::BufferHandle, uint64_t, uint64_t = 0, uint64_t = 0) override {}
        void copy_texture(tgfx::TextureHandle src, tgfx::TextureHandle dst) override {
            state_.texture_copies.emplace_back(src, dst);
        }
        void set_viewport(int, int, int, int) override {}
        void set_scissor(int, int, int, int) override {}

    private:
        ExecutionRecordingState& state_;
    };

    class ExecutionRecordingDevice final : public tgfx::IRenderDevice {
    public:
        ExecutionRecordingState state;

        tgfx::BackendType backend_type() const override {
            return tgfx::BackendType::Vulkan;
        }
        tgfx::BackendCapabilities capabilities() const override {
            tgfx::BackendCapabilities caps;
            caps.backend = tgfx::BackendType::Vulkan;
            caps.supports_multiview = true;
            caps.supports_multisample_resolve = true;
            return caps;
        }
        void wait_idle() override {}
        tgfx::BufferHandle create_buffer(const tgfx::BufferDesc&) override {
            return tgfx::BufferHandle{next_buffer_id_++};
        }
        tgfx::TextureHandle create_texture(const tgfx::TextureDesc& desc) override {
            const tgfx::TextureHandle handle{next_texture_id_++};
            texture_descs_[handle.id] = desc;
            return handle;
        }
        tgfx::SamplerHandle create_sampler(const tgfx::SamplerDesc&) override {
            return tgfx::SamplerHandle{1};
        }
        tgfx::ShaderHandle create_shader(const tgfx::ShaderDesc&) override {
            return tgfx::ShaderHandle{1};
        }
        tgfx::PipelineHandle create_pipeline(const tgfx::PipelineDesc&) override {
            return tgfx::PipelineHandle{1};
        }
        tgfx::ResourceSetHandle create_bound_resource_set(const tgfx::BoundResourceSetDesc&) override {
            return tgfx::ResourceSetHandle{1};
        }
        void destroy(tgfx::BufferHandle) override {}
        void destroy(tgfx::TextureHandle handle) override {
            texture_descs_.erase(handle.id);
        }
        void destroy(tgfx::SamplerHandle) override {}
        void destroy(tgfx::ShaderHandle) override {}
        void destroy(tgfx::PipelineHandle) override {}
        void destroy(tgfx::ResourceSetHandle) override {}
        void upload_buffer(tgfx::BufferHandle, std::span<const uint8_t>, uint64_t = 0) override {}
        void upload_texture(tgfx::TextureHandle, std::span<const uint8_t>, uint32_t = 0) override {}
        void upload_texture_region(tgfx::TextureHandle,
                                   uint32_t,
                                   uint32_t,
                                   uint32_t,
                                   uint32_t,
                                   std::span<const uint8_t>,
                                   uint32_t = 0) override {}
        void read_buffer(tgfx::BufferHandle, std::span<uint8_t>, uint64_t = 0) override {}
        tgfx::TextureDesc texture_desc(tgfx::TextureHandle handle) const override {
            const auto it = texture_descs_.find(handle.id);
            return it == texture_descs_.end() ? tgfx::TextureDesc{} : it->second;
        }
        std::unique_ptr<tgfx::ICommandList> create_command_list(tgfx::QueueType = tgfx::QueueType::Graphics) override {
            return std::make_unique<ExecutionRecordingCommandList>(state);
        }
        void submit(tgfx::ICommandList&) override {}
        void present() override {}

    private:
        uint32_t next_buffer_id_ = 1;
        uint32_t next_texture_id_ = 1;
        std::unordered_map<uint32_t, tgfx::TextureDesc> texture_descs_;
    };

    constexpr const char* kExternalAliasIntermediate = "ExternalAliasIntermediate";

    class RasterTargetInitializer final : public termin::CxxFramePass {
    public:
        RasterTargetInitializer() {
            pass_name_set("RasterTargetInitializer");
        }

        std::set<const char*> compute_writes() const override {
            return {"raster_target_0"};
        }

        std::vector<termin::ResourceSpec> get_resource_specs() const override {
            return {termin::ResourceSpec{"raster_target_0", "fbo", std::pair<int, int>{16, 16}}};
        }

        void execute(termin::ExecuteContext&) override {}
    };

    class RasterFusionProbe final : public termin::CxxFramePass {
    private:
        std::string input_;
        std::string output_;
        tc_raster_load_intent load_;
        bool barrier_after_ = false;

    public:
        RasterFusionProbe(std::string input,
                          std::string output,
                          tc_raster_load_intent load,
                          bool barrier_after = false)
            : input_(std::move(input)),
              output_(std::move(output)),
              load_(load),
              barrier_after_(barrier_after) {
            pass_name_set(output_);
        }

        std::set<const char*> compute_reads() const override {
            return {input_.c_str()};
        }

        std::set<const char*> compute_writes() const override {
            return {output_.c_str()};
        }

        std::vector<std::pair<std::string, std::string>> get_inplace_aliases() const override {
            return {{input_, output_}};
        }

        bool get_raster_contract(termin::ExecuteContext&,
                                 tc_raster_pass_contract& contract) const override {
            contract.target_resource = output_.c_str();
            contract.view_count = 1;
            contract.color_load = load_;
            contract.depth_load = TC_RASTER_LOAD;
            contract.has_color = true;
            contract.has_depth = false;
            contract.attachment_barrier_after = barrier_after_;
            contract.fusion_eligible = true;
            return true;
        }

        bool record_raster(termin::ExecuteContext&) override {
            ++g_raster_probe_record_count;
            return true;
        }

        void execute(termin::ExecuteContext&) override {
            ++g_raster_probe_execute_count;
        }
    };

    class ClearRasterProbe final : public termin::CxxFramePass {
    private:
        std::string resource_;

    public:
        explicit ClearRasterProbe(std::string resource)
            : resource_(std::move(resource)) {
            pass_name_set("ClearRasterProbe");
        }

        std::set<const char*> compute_writes() const override {
            return {resource_.c_str()};
        }

        std::vector<termin::ResourceSpec> get_resource_specs() const override {
            return {termin::ResourceSpec{resource_,
                                         "fbo",
                                         std::pair<int, int>{16, 16},
                                         termin::LinearColor{0.25f, 0.5f, 0.75f, 1.0f},
                                         0.5f}};
        }

        bool get_raster_contract(termin::ExecuteContext&, tc_raster_pass_contract& contract) const override {
            contract.target_resource = resource_.c_str();
            contract.view_count = 1;
            contract.color_load = TC_RASTER_LOAD;
            contract.depth_load = TC_RASTER_LOAD;
            contract.has_color = true;
            contract.has_depth = true;
            contract.fusion_eligible = true;
            return true;
        }

        bool record_raster(termin::ExecuteContext&) override {
            ++g_raster_probe_record_count;
            return true;
        }

        void execute(termin::ExecuteContext&) override {
            ++g_raster_probe_execute_count;
        }
    };

    class ClearedResourceReadProbe final : public termin::CxxFramePass {
    private:
        std::string input_;
        std::string output_;

    public:
        ClearedResourceReadProbe(std::string input, std::string output)
            : input_(std::move(input)),
              output_(std::move(output)) {
            pass_name_set("ClearedResourceReadProbe");
        }

        std::set<const char*> compute_reads() const override {
            return {input_.c_str()};
        }

        std::set<const char*> compute_writes() const override {
            return {output_.c_str()};
        }

        std::vector<std::pair<std::string, std::string>> get_inplace_aliases() const override {
            return {{input_, output_}};
        }

        std::vector<termin::ResourceSpec> get_resource_specs() const override {
            return {termin::ResourceSpec{input_,
                                         "fbo",
                                         std::pair<int, int>{16, 16},
                                         termin::LinearColor{0.1f, 0.2f, 0.3f, 1.0f},
                                         0.75f}};
        }

        void execute(termin::ExecuteContext&) override {}
    };

    class MsaaTargetInitializer final : public termin::CxxFramePass {
    public:
        MsaaTargetInitializer() {
            pass_name_set("MsaaTargetInitializer");
        }

        std::set<const char*> compute_writes() const override {
            return {"msaa_target_0"};
        }

        std::vector<termin::ResourceSpec> get_resource_specs() const override {
            return {termin::ResourceSpec{"msaa_target_0",
                                         "fbo",
                                         std::pair<int, int>{16, 16},
                                         std::nullopt,
                                         std::nullopt,
                                         std::string{"rgba16f"},
                                         4}};
        }

        void execute(termin::ExecuteContext&) override {}
    };

    class ResolveFusionProbe final : public termin::CxxFramePass {
    private:
        std::string target_;
        bool declare_target_ = true;

    public:
        explicit ResolveFusionProbe(std::string target = "resolved_target", bool declare_target = true)
            : target_(std::move(target)),
              declare_target_(declare_target) {
            pass_name_set("ResolveFusionProbe");
        }

        std::set<const char*> compute_reads() const override {
            return {"msaa_target_2"};
        }

        std::set<const char*> compute_writes() const override {
            return {target_.c_str()};
        }

        std::vector<termin::ResourceSpec> get_resource_specs() const override {
            if (!declare_target_)
                return {};
            return {termin::ResourceSpec{target_,
                                         "fbo",
                                         std::pair<int, int>{16, 16},
                                         std::nullopt,
                                         std::nullopt,
                                         std::string{"rgba16f"},
                                         1}};
        }

        bool get_raster_resolve_contract(termin::ExecuteContext&,
                                         tc_raster_resolve_contract& contract) const override {
            contract.source_resource = "msaa_target_2";
            contract.target_resource = target_.c_str();
            contract.view_count = 1;
            contract.fusion_eligible = true;
            return true;
        }

        void execute(termin::ExecuteContext&) override {
            ++g_resolve_probe_execute_count;
        }
    };

    class ExternalAliasProducer final : public termin::CxxFramePass {
    public:
        ExternalAliasProducer() {
            pass_name_set("ExternalAliasProducer");
        }

        std::set<const char*> compute_writes() const override {
            return {kExternalAliasIntermediate};
        }

        void execute(termin::ExecuteContext& context) override {
            const auto output = context.tex2_writes.find(kExternalAliasIntermediate);
            REQUIRE(output != context.tex2_writes.end());
            CHECK_EQ(output->second.id, 777u);
            g_external_alias_producer_executed = true;
        }
    };

    class ExternalAliasConsumer final : public termin::CxxFramePass {
    public:
        ExternalAliasConsumer() {
            pass_name_set("ExternalAliasConsumer");
        }

        std::set<const char*> compute_reads() const override {
            return {kExternalAliasIntermediate};
        }

        std::set<const char*> compute_writes() const override {
            return {"OUTPUT"};
        }

        std::vector<std::pair<std::string, std::string>> get_inplace_aliases() const override {
            return {{kExternalAliasIntermediate, "OUTPUT"}};
        }

        void execute(termin::ExecuteContext& context) override {
            const auto input = context.tex2_reads.find(kExternalAliasIntermediate);
            const auto output = context.tex2_writes.find("OUTPUT");
            REQUIRE(input != context.tex2_reads.end());
            REQUIRE(output != context.tex2_writes.end());
            CHECK_EQ(input->second.id, 777u);
            CHECK_EQ(output->second.id, 777u);
            g_external_alias_consumer_executed = true;
        }
    };

    class TestFrameGraphResource final : public termin::FrameGraphResource {
    public:
        int extent = 0;

        explicit TestFrameGraphResource(int extent_value)
            : extent(extent_value) {}

        ~TestFrameGraphResource() override {
            ++g_resource_destroy_count;
        }

        const char* resource_type() const override {
            return kTestResourceType;
        }
    };

    termin::FrameGraphResource* create_test_resource(const termin::ResourceSpec& spec) {
        ++g_resource_create_count;
        return new TestFrameGraphResource(spec.size ? spec.size->first : 0);
    }

    termin::FrameGraphResourceSampledTexture preview_test_resource(const termin::FrameGraphResource& resource) {
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
            auto* resource = context.get_frame_graph_resource_as<TestFrameGraphResource>("test_resource");
            REQUIRE(resource != nullptr);
            CHECK(resource->extent == 17);
            CHECK(context.tex2_reads.find("test_resource") == context.tex2_reads.end());
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

        std::vector<std::pair<std::string, std::string>> get_inplace_aliases() const override {
            return {{"test_resource", "test_resource_alias"}};
        }

        void execute(termin::ExecuteContext& context) override {
            auto* original = context.get_frame_graph_resource_as<TestFrameGraphResource>("test_resource");
            auto* alias = context.get_frame_graph_resource_as<TestFrameGraphResource>("test_resource_alias");
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

    void publish_empty_snapshot(termin::RenderItemSnapshot& snapshot) {
        InMemoryRenderItemSource source(0);
        REQUIRE(source.publish(snapshot, {}));
    }

    void register_probe() {
        if (!tc_pass_registry_has("CxxFramePass")) {
            termin::register_builtin_render_pass_types();
        }
        tc_pass_registry_unregister(kProbeType);
        auto descriptor =
            termin::FramePassTypeDescriptorBuilder::native<GenericExecutionProbe>(kProbeType, "termin-render-test");
        REQUIRE(descriptor.commit());
    }

    TEST_CASE("generic pipeline rejects a missing published snapshot observably") {
        termin::RenderPipeline pipeline("generic-execution-missing-snapshot");
        REQUIRE(pipeline.is_valid());
        termin::RenderTargetContext target;
        target.name = "MissingSnapshotTarget";

        termin::RenderExecution execution;
        execution.pipeline = &pipeline;
        execution.targets.emplace(target.name,
                                  termin::RenderExecutionTarget{
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

TEST_CASE("color output binding planner separates direct copy transform and scene rejection") {
    tgfx::TextureDesc rgba16;
    rgba16.width = 640;
    rgba16.height = 480;
    rgba16.format = tgfx::PixelFormat::RGBA16F;

    tgfx::TextureDesc linear8 = rgba16;
    linear8.format = tgfx::PixelFormat::RGBA8_UNorm;
    tgfx::TextureDesc srgb8 = linear8;
    srgb8.format = tgfx::PixelFormat::RGBA8_sRGB;

    CHECK(termin::plan_color_output_binding(rgba16, termin::ColorContent::DisplayLinear, rgba16).operation ==
          termin::ColorOutputBindingOp::Direct);
    CHECK(termin::plan_color_output_binding(rgba16, termin::ColorContent::DisplayLinear, linear8).operation ==
          termin::ColorOutputBindingOp::Transform);
    CHECK(termin::plan_color_output_binding(rgba16, termin::ColorContent::DisplayLinear, srgb8).operation ==
          termin::ColorOutputBindingOp::Transform);
    CHECK(termin::plan_color_output_binding(srgb8, termin::ColorContent::DisplaySRGB, srgb8).operation ==
          termin::ColorOutputBindingOp::Direct);
    CHECK(termin::plan_color_output_binding(srgb8, termin::ColorContent::DisplaySRGB, linear8).operation ==
          termin::ColorOutputBindingOp::Transform);

    const auto srgb_params =
        termin::make_output_transform_params(rgba16, termin::ColorContent::DisplayLinear, srgb8);
    CHECK(srgb_params.sampled_input_encoding == tgfx::TextureEncoding::Linear);
    CHECK(srgb_params.target_encoding == tgfx::TextureEncoding::SRGB);
    CHECK(srgb_params.dither == tgfx::OutputDitherMode::StableSpatial);
    CHECK(srgb_params.target_rgb_bits == 8);

    const auto linear_params =
        termin::make_output_transform_params(srgb8, termin::ColorContent::DisplaySRGB, linear8);
    CHECK(linear_params.sampled_input_encoding == tgfx::TextureEncoding::Linear);
    CHECK(linear_params.target_encoding == tgfx::TextureEncoding::Linear);
    CHECK(linear_params.dither == tgfx::OutputDitherMode::StableSpatial);
    CHECK(linear_params.target_rgb_bits == 8);

    const auto rejected = termin::plan_color_output_binding(rgba16, termin::ColorContent::SceneLinear, linear8);
    CHECK_FALSE(rejected.valid);
    CHECK(rejected.operation == termin::ColorOutputBindingOp::RejectSceneLinear);
    CHECK(termin::plan_color_output_binding(rgba16, termin::ColorContent::SceneLinear, rgba16).operation ==
          termin::ColorOutputBindingOp::Direct);

    tgfx::TextureDesc msaa = rgba16;
    msaa.sample_count = 4;
    CHECK(termin::plan_color_output_binding(msaa, termin::ColorContent::DisplayLinear, rgba16).operation ==
          termin::ColorOutputBindingOp::CopyOrResolve);

    tgfx::TextureDesc multiview_source = rgba16;
    multiview_source.array_layers = 2;
    tgfx::TextureDesc multiview_target = srgb8;
    multiview_target.array_layers = 2;
    CHECK(termin::plan_color_output_binding(multiview_source,
                                            termin::ColorContent::DisplayLinear,
                                            multiview_target)
              .operation == termin::ColorOutputBindingOp::Transform);
}

TEST_CASE("compatible color export is bound directly to the physical target") {
    termin::RenderPipeline pipeline("direct-color-export-test");
    REQUIRE(pipeline.is_valid());
    pipeline.add_pass((new ClearRasterProbe("final_color"))->tc_pass_ptr());
    termin::ResourceSpec export_spec;
    export_spec.resource = "final_color";
    export_spec.format = "rgba16f";
    export_spec.size = {16, 16};
    pipeline.add_spec(export_spec);
    pipeline.set_color_export("final_color", termin::ColorContent::DisplayLinear);

    auto device = std::make_unique<ExecutionRecordingDevice>();
    ExecutionRecordingDevice* recording_device = device.get();
    tgfx::TextureDesc output_desc;
    output_desc.width = 16;
    output_desc.height = 16;
    output_desc.format = tgfx::PixelFormat::RGBA16F;
    output_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopyDst;
    const tgfx::TextureHandle output = device->create_texture(output_desc);
    auto host = tgfx::GraphicsHost::adopt_isolated_device(std::move(device));

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "DirectColorTarget";
    target.render_rect = {0, 0, 16, 16};
    target.output_color.texture = output;
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
                                  .context = &target,
                                  .render_items = &snapshot,
                              });

    termin::RenderEngine engine;
    engine.set_graphics_host(*host);
    engine.execute_pipeline(execution);

    REQUIRE(recording_device->state.scopes.size() == 1u);
    REQUIRE(recording_device->state.scopes[0].pass.colors.size() == 1u);
    CHECK(recording_device->state.scopes[0].pass.colors[0].texture == output);
    CHECK(recording_device->state.texture_copies.empty());
    pipeline.destroy();
}

TEST_CASE("incompatible float color export records one copy epilogue") {
    termin::RenderPipeline pipeline("color-export-epilogue-test");
    REQUIRE(pipeline.is_valid());
    pipeline.add_pass((new ClearRasterProbe("final_color"))->tc_pass_ptr());
    termin::ResourceSpec export_spec;
    export_spec.resource = "final_color";
    export_spec.format = "rgba16f";
    export_spec.size = {16, 16};
    pipeline.add_spec(export_spec);
    pipeline.set_color_export("final_color", termin::ColorContent::DisplayLinear);

    auto device = std::make_unique<ExecutionRecordingDevice>();
    ExecutionRecordingDevice* recording_device = device.get();
    tgfx::TextureDesc output_desc;
    output_desc.width = 16;
    output_desc.height = 16;
    output_desc.format = tgfx::PixelFormat::RGBA32F;
    output_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopyDst;
    const tgfx::TextureHandle output = device->create_texture(output_desc);
    auto host = tgfx::GraphicsHost::adopt_isolated_device(std::move(device));

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "FloatColorTarget";
    target.render_rect = {0, 0, 16, 16};
    target.output_color.texture = output;
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
                                  .context = &target,
                                  .render_items = &snapshot,
                              });

    termin::RenderEngine engine;
    engine.set_graphics_host(*host);
    engine.execute_pipeline(execution);

    REQUIRE(recording_device->state.scopes.size() == 1u);
    REQUIRE(recording_device->state.scopes[0].pass.colors.size() == 1u);
    CHECK(recording_device->state.scopes[0].pass.colors[0].texture != output);
    REQUIRE(recording_device->state.texture_copies.size() == 1u);
    CHECK(recording_device->state.texture_copies[0].second == output);
    pipeline.destroy();
}

TEST_CASE("one color export resource fans out through epilogues instead of direct rebinding") {
    termin::RenderPipeline pipeline("color-export-fanout-test");
    REQUIRE(pipeline.is_valid());
    pipeline.add_pass((new ClearRasterProbe("final_color"))->tc_pass_ptr());
    termin::ResourceSpec export_spec;
    export_spec.resource = "final_color";
    export_spec.format = "rgba16f";
    export_spec.size = {16, 16};
    pipeline.add_spec(export_spec);
    pipeline.set_color_export("final_color", termin::ColorContent::DisplayLinear, "TargetA");
    pipeline.set_color_export("final_color", termin::ColorContent::DisplayLinear, "TargetB");

    auto device = std::make_unique<ExecutionRecordingDevice>();
    ExecutionRecordingDevice* recording_device = device.get();
    tgfx::TextureDesc output_desc;
    output_desc.width = 16;
    output_desc.height = 16;
    output_desc.format = tgfx::PixelFormat::RGBA16F;
    output_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopyDst;
    const tgfx::TextureHandle output_a = device->create_texture(output_desc);
    const tgfx::TextureHandle output_b = device->create_texture(output_desc);
    auto host = tgfx::GraphicsHost::adopt_isolated_device(std::move(device));

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target_a;
    target_a.name = "TargetA";
    target_a.render_rect = {0, 0, 16, 16};
    target_a.output_color.texture = output_a;
    termin::RenderTargetContext target_b;
    target_b.name = "TargetB";
    target_b.render_rect = {0, 0, 16, 16};
    target_b.output_color.texture = output_b;
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target_a.name;
    execution.targets.emplace(target_a.name,
                              termin::RenderExecutionTarget{
                                  .context = &target_a,
                                  .render_items = &snapshot,
                              });
    execution.targets.emplace(target_b.name,
                              termin::RenderExecutionTarget{
                                  .context = &target_b,
                                  .render_items = &snapshot,
                              });

    termin::RenderEngine engine;
    engine.set_graphics_host(*host);
    engine.execute_pipeline(execution);

    REQUIRE(recording_device->state.scopes.size() == 1u);
    REQUIRE(recording_device->state.scopes[0].pass.colors.size() == 1u);
    const tgfx::TextureHandle internal_output = recording_device->state.scopes[0].pass.colors[0].texture;
    CHECK(internal_output != output_a);
    CHECK(internal_output != output_b);
    REQUIRE(recording_device->state.texture_copies.size() == 2u);
    CHECK(recording_device->state.texture_copies[0].first == internal_output);
    CHECK(recording_device->state.texture_copies[0].second == output_a);
    CHECK(recording_device->state.texture_copies[1].first == internal_output);
    CHECK(recording_device->state.texture_copies[1].second == output_b);
    pipeline.destroy();
}

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
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
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

TEST_CASE("inplace aliases preserve caller-owned external outputs") {
    termin::RenderPipeline pipeline("external-output-alias-test");
    REQUIRE(pipeline.is_valid());
    auto* producer = new ExternalAliasProducer();
    auto* consumer = new ExternalAliasConsumer();
    pipeline.add_pass(producer->tc_pass_ptr());
    pipeline.add_pass(consumer->tc_pass_ptr());

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "ExternalAliasTarget";
    target.render_rect = {0, 0, 1, 1};
    target.output_color.texture = tgfx::TextureHandle{777};

    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
                                  .context = &target,
                                  .render_items = &snapshot,
                              });

    g_external_alias_producer_executed = false;
    g_external_alias_consumer_executed = false;
    termin::RenderEngine engine;
    engine.execute_pipeline(execution);
    CHECK(g_external_alias_producer_executed);
    CHECK(g_external_alias_consumer_executed);

    pipeline.destroy();
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
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
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

TEST_CASE("adjacent compatible raster passes record inside one physical scope") {
    termin::RenderPipeline pipeline("raster-fusion-execution-test");
    REQUIRE(pipeline.is_valid());
    pipeline.add_pass((new RasterTargetInitializer())->tc_pass_ptr());
    pipeline.add_pass((new RasterFusionProbe("raster_target_0", "raster_target_1", TC_RASTER_CLEAR))->tc_pass_ptr());
    pipeline.add_pass((new RasterFusionProbe("raster_target_1", "raster_target_2", TC_RASTER_LOAD))->tc_pass_ptr());

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "RasterFusionTarget";
    target.render_rect = {0, 0, 16, 16};
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
                                  .context = &target,
                                  .render_items = &snapshot,
                              });

    g_raster_probe_execute_count = 0;
    g_raster_probe_record_count = 0;
    termin::RenderEngine engine;
    engine.execute_pipeline(execution);
    CHECK(g_raster_probe_execute_count == 0);
    CHECK(g_raster_probe_record_count == 2);

    pipeline.destroy();
}

TEST_CASE("compatible raster clear metadata becomes the physical scope load operation") {
    termin::RenderPipeline pipeline("raster-deferred-clear-execution-test");
    REQUIRE(pipeline.is_valid());
    pipeline.add_pass((new ClearRasterProbe("clear_target"))->tc_pass_ptr());

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "RasterDeferredClearTarget";
    target.render_rect = {0, 0, 16, 16};
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
                                  .context = &target,
                                  .render_items = &snapshot,
                              });

    auto device = std::make_unique<ExecutionRecordingDevice>();
    ExecutionRecordingDevice* recording_device = device.get();
    auto host = tgfx::GraphicsHost::adopt_isolated_device(std::move(device));
    termin::RenderEngine engine;
    engine.set_graphics_host(*host);
    g_raster_probe_execute_count = 0;
    g_raster_probe_record_count = 0;
    engine.execute_pipeline(execution);

    CHECK(g_raster_probe_execute_count == 0);
    CHECK(g_raster_probe_record_count == 1);
    REQUIRE(recording_device->state.scopes.size() == 1);
    const tgfx::RenderPassDesc& scope = recording_device->state.scopes.front().pass;
    REQUIRE(scope.colors.size() == 1);
    CHECK(scope.colors.front().load == tgfx::LoadOp::Clear);
    CHECK(scope.colors.front().clear_color.r == guard::Approx(0.25f));
    CHECK(scope.colors.front().clear_color.g == guard::Approx(0.5f));
    CHECK(scope.colors.front().clear_color.b == guard::Approx(0.75f));
    CHECK(scope.has_depth);
    CHECK(scope.depth.load == tgfx::LoadOp::Clear);
    CHECK(scope.depth.clear_depth == guard::Approx(0.5f));

    pipeline.destroy();
}

TEST_CASE("resource read before raster write keeps the standalone clear") {
    termin::RenderPipeline pipeline("raster-read-before-clear-execution-test");
    REQUIRE(pipeline.is_valid());
    pipeline.add_pass((new ClearedResourceReadProbe("clear_target", "clear_target_after_read"))->tc_pass_ptr());
    pipeline.add_pass((new RasterFusionProbe(
                           "clear_target_after_read", "clear_target_after_raster", TC_RASTER_LOAD))
                          ->tc_pass_ptr());

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "RasterReadBeforeClearTarget";
    target.render_rect = {0, 0, 16, 16};
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
                                  .context = &target,
                                  .render_items = &snapshot,
                              });

    auto device = std::make_unique<ExecutionRecordingDevice>();
    ExecutionRecordingDevice* recording_device = device.get();
    auto host = tgfx::GraphicsHost::adopt_isolated_device(std::move(device));
    termin::RenderEngine engine;
    engine.set_graphics_host(*host);
    engine.execute_pipeline(execution);

    REQUIRE(recording_device->state.scopes.size() == 2);
    const tgfx::RenderPassDesc& clear_scope = recording_device->state.scopes[0].pass;
    REQUIRE(clear_scope.colors.size() == 1);
    CHECK(clear_scope.colors.front().load == tgfx::LoadOp::Clear);
    CHECK(clear_scope.has_depth);
    CHECK(clear_scope.depth.load == tgfx::LoadOp::Clear);
    const tgfx::RenderPassDesc& raster_scope = recording_device->state.scopes[1].pass;
    REQUIRE(raster_scope.colors.size() == 1);
    CHECK(raster_scope.colors.front().load == tgfx::LoadOp::Load);

    pipeline.destroy();
}

TEST_CASE("fused logical raster boundary records the requested attachment barrier") {
    termin::RenderPipeline pipeline("raster-fusion-barrier-execution-test");
    REQUIRE(pipeline.is_valid());
    pipeline.add_pass((new RasterTargetInitializer())->tc_pass_ptr());
    pipeline.add_pass(
        (new RasterFusionProbe("raster_target_0", "raster_target_1", TC_RASTER_CLEAR, true))->tc_pass_ptr());
    pipeline.add_pass((new RasterFusionProbe("raster_target_1", "raster_target_2", TC_RASTER_LOAD))->tc_pass_ptr());

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "RasterFusionBarrierTarget";
    target.render_rect = {0, 0, 16, 16};
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
                                  .context = &target,
                                  .render_items = &snapshot,
                              });

    auto device = std::make_unique<ExecutionRecordingDevice>();
    ExecutionRecordingDevice* recording_device = device.get();
    auto host = tgfx::GraphicsHost::adopt_isolated_device(std::move(device));
    termin::RenderEngine engine;
    engine.set_graphics_host(*host);
    engine.execute_pipeline(execution);

    CHECK(recording_device->state.framebuffer_local_barriers == 1);

    pipeline.destroy();
}

TEST_CASE("compatible resolve is absorbed into the fused raster scope") {
    termin::RenderPipeline pipeline("raster-resolve-fusion-execution-test");
    REQUIRE(pipeline.is_valid());
    pipeline.add_pass((new MsaaTargetInitializer())->tc_pass_ptr());
    pipeline.add_pass((new RasterFusionProbe("msaa_target_0", "msaa_target_1", TC_RASTER_CLEAR))->tc_pass_ptr());
    pipeline.add_pass((new RasterFusionProbe("msaa_target_1", "msaa_target_2", TC_RASTER_LOAD))->tc_pass_ptr());
    pipeline.add_pass((new ResolveFusionProbe())->tc_pass_ptr());

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "RasterResolveFusionTarget";
    target.render_rect = {0, 0, 16, 16};
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
                                  .context = &target,
                                  .render_items = &snapshot,
                              });

    g_raster_probe_execute_count = 0;
    g_raster_probe_record_count = 0;
    g_resolve_probe_execute_count = 0;
    termin::RenderEngine engine;
    engine.execute_pipeline(execution);
    CHECK(g_raster_probe_execute_count == 0);
    CHECK(g_raster_probe_record_count == 2);
    CHECK(g_resolve_probe_execute_count == 0);

    pipeline.destroy();
}

TEST_CASE("first-access resolve suppresses an external target preclear") {
    termin::RenderPipeline pipeline("external-resolve-clear-elision-test");
    REQUIRE(pipeline.is_valid());
    pipeline.add_pass((new MsaaTargetInitializer())->tc_pass_ptr());
    pipeline.add_pass((new RasterFusionProbe("msaa_target_0", "msaa_target_1", TC_RASTER_CLEAR))->tc_pass_ptr());
    pipeline.add_pass((new RasterFusionProbe("msaa_target_1", "msaa_target_2", TC_RASTER_LOAD))->tc_pass_ptr());
    pipeline.add_pass((new ResolveFusionProbe("OUTPUT", false))->tc_pass_ptr());

    auto device = std::make_unique<ExecutionRecordingDevice>();
    ExecutionRecordingDevice* recording_device = device.get();
    tgfx::TextureDesc output_desc;
    output_desc.width = 16;
    output_desc.height = 16;
    output_desc.format = tgfx::PixelFormat::RGBA16F;
    output_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopyDst;
    const tgfx::TextureHandle output = device->create_texture(output_desc);
    auto host = tgfx::GraphicsHost::adopt_isolated_device(std::move(device));

    termin::RenderItemSnapshot snapshot;
    publish_empty_snapshot(snapshot);
    termin::RenderTargetContext target;
    target.name = "ExternalResolveClearElisionTarget";
    target.render_rect = {0, 0, 16, 16};
    target.output_color.texture = output;
    target.clear_color_enabled = true;
    target.clear_linear_color = {0.8f, 0.2f, 0.1f, 1.0f};
    termin::RenderExecution execution;
    execution.pipeline = &pipeline;
    execution.default_render_target = target.name;
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
                                  .context = &target,
                                  .render_items = &snapshot,
                              });

    termin::RenderEngine engine;
    engine.set_graphics_host(*host);
    engine.execute_pipeline(execution);

    REQUIRE(recording_device->state.scopes.size() == 1);
    const tgfx::RenderPassDesc& scope = recording_device->state.scopes.front().pass;
    REQUIRE(scope.colors.size() == 1);
    CHECK(scope.colors.front().resolve_texture == output);

    pipeline.destroy();
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
    execution.targets.emplace(target.name,
                              termin::RenderExecutionTarget{
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
