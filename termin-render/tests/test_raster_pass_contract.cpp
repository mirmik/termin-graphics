#include "guard_main.h"

GUARD_TEST_MAIN();

#include <type_traits>
#include <string_view>

#include <termin/render/execute_context.hpp>
#include <termin/render/frame_pass.hpp>

namespace {

    class RasterProbePass final : public termin::CxxFramePass {
    public:
        int standalone_calls = 0;
        int raster_calls = 0;
        bool eligible = true;

        void execute(termin::ExecuteContext&) override {
            ++standalone_calls;
        }

        bool get_raster_contract(termin::ExecuteContext&,
                                 tc_raster_pass_contract& contract) const override {
            contract = {};
            contract.struct_size = sizeof(contract);
            contract.target_resource = "probe_target";
            contract.view_count = 1;
            contract.color_load = TC_RASTER_LOAD;
            contract.depth_load = TC_RASTER_CLEAR;
            contract.has_color = true;
            contract.has_depth = true;
            contract.attachment_barrier_after = true;
            contract.fusion_eligible = eligible;
            return true;
        }

        bool record_raster(termin::ExecuteContext&) override {
            ++raster_calls;
            return true;
        }
    };

    class ResolveProbePass final : public termin::CxxFramePass {
    public:
        int standalone_calls = 0;

        void execute(termin::ExecuteContext&) override {
            ++standalone_calls;
        }

        bool get_raster_resolve_contract(termin::ExecuteContext&,
                                         tc_raster_resolve_contract& contract) const override {
            contract = {};
            contract.struct_size = sizeof(contract);
            contract.source_resource = "msaa_color";
            contract.target_resource = "resolved_color";
            contract.view_count = 2;
            contract.fusion_eligible = true;
            return true;
        }
    };

} // namespace

static_assert(std::is_standard_layout_v<tc_raster_pass_contract>);
static_assert(std::is_trivially_copyable_v<tc_raster_pass_contract>);
static_assert(std::is_standard_layout_v<tc_raster_resolve_contract>);
static_assert(std::is_trivially_copyable_v<tc_raster_resolve_contract>);

TEST_CASE("raster pass ABI keeps standalone execution separate from recording") {
    RasterProbePass pass;
    termin::ExecuteContext context;
    tc_raster_pass_contract contract{};

    REQUIRE(tc_pass_get_raster_contract(pass.tc_pass_ptr(), &context, &contract));
    CHECK(contract.struct_size == sizeof(tc_raster_pass_contract));
    CHECK(std::string_view(contract.target_resource) == "probe_target");
    CHECK(contract.view_count == 1);
    CHECK(contract.color_load == TC_RASTER_LOAD);
    CHECK(contract.depth_load == TC_RASTER_CLEAR);
    CHECK(contract.attachment_barrier_after);
    CHECK(contract.fusion_eligible);

    CHECK(tc_pass_record_raster(pass.tc_pass_ptr(), &context));
    CHECK(pass.raster_calls == 1);
    CHECK(pass.standalone_calls == 0);

    tc_pass_execute(pass.tc_pass_ptr(), &context);
    CHECK(pass.standalone_calls == 1);
    CHECK(pass.raster_calls == 1);
}

TEST_CASE("passes without raster opt-in remain standalone") {
    termin::CxxFramePass pass;
    termin::ExecuteContext context;
    tc_raster_pass_contract contract{};

    CHECK(!tc_pass_get_raster_contract(pass.tc_pass_ptr(), &context, &contract));
    CHECK(contract.struct_size == sizeof(tc_raster_pass_contract));
    CHECK(!tc_pass_record_raster(pass.tc_pass_ptr(), &context));
    tc_raster_resolve_contract resolve_contract{};
    CHECK(!tc_pass_get_raster_resolve_contract(pass.tc_pass_ptr(), &context, &resolve_contract));
    CHECK(resolve_contract.struct_size == sizeof(tc_raster_resolve_contract));
}

TEST_CASE("resolve absorption query does not replace standalone execution") {
    ResolveProbePass pass;
    termin::ExecuteContext context;
    tc_raster_resolve_contract contract{};

    REQUIRE(tc_pass_get_raster_resolve_contract(pass.tc_pass_ptr(), &context, &contract));
    CHECK(contract.struct_size == sizeof(tc_raster_resolve_contract));
    CHECK(std::string_view(contract.source_resource) == "msaa_color");
    CHECK(std::string_view(contract.target_resource) == "resolved_color");
    CHECK(contract.view_count == 2);
    CHECK(contract.fusion_eligible);

    tc_pass_execute(pass.tc_pass_ptr(), &context);
    CHECK(pass.standalone_calls == 1);
}
