#include "guard_main.h"

GUARD_TEST_MAIN();

#include "project_surface_test_plugin.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <termin/materials/surface_contract_registry.h>
#include <termin/render/material_pipeline.hpp>
#include <termin/render/material_pipeline_shader_assembler.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

namespace {

    constexpr const char* kEvaluatorSource = R"slang(
struct FragmentInput {
    float4 screen_pos : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal_world : TEXCOORD1;
};

struct WeatherMaterial {
    float4 base_color;
    float weathering;
};

[[TerminScope("material")]]
ConstantBuffer<WeatherMaterial> weather_material;

TestWeatheredSurfaceV1 evaluate_weathered_surface(FragmentInput input) {
    TestWeatheredSurfaceV1 surface;
    surface.base_color = weather_material.base_color.rgb;
    surface.normal_world = normalize(input.normal_world);
    surface.weathering = saturate(
        weather_material.weathering + input.world_pos.y * 0.0);
    return surface;
}
)slang";

    constexpr const char* kConsumerSource = R"slang(
struct WeatherPass {
    float weathering_gain;
};

[[TerminScope("pass")]]
ConstantBuffer<WeatherPass> weather_pass;

struct FragmentOutput {
    float4 color : SV_Target0;
};

[shader("fragment")]
FragmentOutput consume_weathered_surface(FragmentInput input) {
    TestWeatheredSurfaceV1 surface = evaluate_weathered_surface(input);
    FragmentOutput output;
    float weathering = saturate(surface.weathering * weather_pass.weathering_gain);
    output.color = float4(
        lerp(surface.base_color, abs(surface.normal_world), weathering),
        1.0);
    return output;
}
)slang";

    struct ScopedArtifactConfiguration {
        std::filesystem::path root;

        ScopedArtifactConfiguration() {
            const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
            root =
                std::filesystem::temp_directory_path() / ("termin-project-surface-contract-" + std::to_string(unique));
            std::filesystem::remove_all(root);
            termin::tgfx2_set_shader_artifact_root(root.string().c_str());
            termin::tgfx2_set_shader_cache_root("");
#ifdef TERMIN_PROJECT_SURFACE_TEST_SHADERC
            termin::tgfx2_set_shader_compiler_path(TERMIN_PROJECT_SURFACE_TEST_SHADERC);
#endif
            termin::tgfx2_set_shader_dev_compile_enabled(true);
        }

        ~ScopedArtifactConfiguration() {
            termin::tgfx2_set_shader_dev_compile_enabled(false);
            termin::tgfx2_set_shader_compiler_path("");
            termin::tgfx2_set_shader_cache_root("");
            termin::tgfx2_set_shader_artifact_root("");
            std::filesystem::remove_all(root);
        }
    };

    const tc_shader_resource_requirement* contract_resource(const tc_shader_contract_view& contract, const char* name) {
        for (uint32_t i = 0; i < contract.resource_count; ++i) {
            if (std::strcmp(contract.resources[i].name, name) == 0) {
                return &contract.resources[i];
            }
        }
        return nullptr;
    }

    termin::TcShader make_weathered_producer() {
        tc_shader_fragment_input fragment_inputs[2]{};
        std::snprintf(fragment_inputs[0].semantic, sizeof(fragment_inputs[0].semantic), "%s", "world_pos");
        fragment_inputs[0].type = TC_SHADER_CONTRACT_VALUE_FLOAT3;
        std::snprintf(fragment_inputs[1].semantic, sizeof(fragment_inputs[1].semantic), "%s", "normal_world");
        fragment_inputs[1].type = TC_SHADER_CONTRACT_VALUE_FLOAT3;

        tc_shader_resource_requirement resources[1]{};
        std::snprintf(resources[0].name, sizeof(resources[0].name), "%s", "weather_material");
        resources[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
        resources[0].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
        resources[0].stage_mask = TC_SHADER_STAGE_FRAGMENT;
        resources[0].size = 32u;

        const tc_shader_surface_producer_desc producer = {
            TC_SHADER_SURFACE_PRODUCER_SCHEMA_VERSION,
            "test.surface.weathered",
            1u,
            "TestWeatheredSurfaceV1",
            "evaluate_weathered_surface",
            kEvaluatorSource,
            "test.surface.weathered@1:evaluator:v1",
            fragment_inputs,
            2u,
            resources,
            1u,
        };

        termin::TcShaderCreateInfo create_info{};
        create_info.sources.fragment = kEvaluatorSource;
        create_info.sources.name = "Project Weathered Surface Producer";
        create_info.sources.fragment_entry = "evaluate_weathered_surface";
        create_info.uuid = "test-project-weathered-producer";
        create_info.language = TC_SHADER_LANGUAGE_SLANG;
        create_info.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
        create_info.surface_producer = &producer;
        return termin::TcShader::from_sources(create_info);
    }

    termin::MaterialPipelinePassContract weathered_pass_contract() {
        termin::MaterialPipelinePassContract pass{};
        pass.debug_name = "project_weathered_consumer";
        pass.fragment_composition = termin::MaterialFragmentComposition::SurfaceConsumer;
        pass.vertex_output_adapter = termin::material_pipeline_standard_material_vertex_output_adapter();
        pass.static_vertex_transform = termin::material_pipeline_make_static_mesh_vertex_transform_provider(
            "project_weathered_static", termin::MeshVertexTransformProfile::Material, "draw_data.u_model");
        pass.static_vertex_transform->resources.push_back(
            termin::material_pipeline_draw_resource_decl("draw_data", TC_SHADER_STAGE_VERTEX, 64u));

        termin::MaterialSurfaceConsumerContract consumer{};
        consumer.accepted_surface = {"test.surface.weathered", 1u};
        consumer.consumer_source = kConsumerSource;
        consumer.fragment_entry = "consume_weathered_surface";
        consumer.source_identity = "test.surface.weathered@1:consumer:v1";
        consumer.required_fragment_input.semantics = {
            {"world_pos", termin::MaterialPipelineValueType::Float3},
            {"normal_world", termin::MaterialPipelineValueType::Float3},
        };
        consumer.resources.push_back({
            {
                "weather_pass",
                TC_SHADER_RESOURCE_CONSTANT_BUFFER,
                TC_SHADER_RESOURCE_SCOPE_PASS,
                TC_SHADER_STAGE_FRAGMENT,
                16u,
            },
            termin::MaterialPipelineResourceOwner::Pass,
        });
        pass.surface_consumer = std::move(consumer);
        return pass;
    }

} // namespace

TEST_CASE("project-owned surface contract composes compiles and unloads safely") {
    tc_shader_init();
    tc_surface_contract_registry_clear();
    REQUIRE(tc_surface_contract_registry_register_builtins());

    const tc_surface_contract_key standard_key = {
        TC_STANDARD_PBR_SURFACE_CONTRACT_ID,
        TC_STANDARD_PBR_SURFACE_CONTRACT_VERSION,
    };
    const tc_surface_contract_desc* standard_before = tc_surface_contract_registry_find(standard_key);
    REQUIRE(standard_before != nullptr);
    const std::string standard_identity = standard_before->source_identity;
    const std::string standard_interface = standard_before->interface_source;

    REQUIRE(termin_project_surface_test_plugin_register());
    const tc_surface_contract_key project_key = {
        "test.surface.weathered",
        1u,
    };
    const tc_surface_contract_desc* project = tc_surface_contract_registry_find(project_key);
    REQUIRE(project != nullptr);
    CHECK(std::strcmp(tc_surface_contract_registry_owner(project_key), termin_project_surface_test_plugin_owner()) ==
          0);
    CHECK(std::strstr(project->interface_source, "float weathering") != nullptr);
    CHECK_EQ(tc_surface_contract_registry_count(), 2u);

    termin::TcShader producer = make_weathered_producer();
    REQUIRE(producer.is_valid());
    REQUIRE(producer.has_surface_producer());
    termin::MaterialPipelinePassContract pass = weathered_pass_contract();

    termin::MaterialShaderOverrideRequest request{};
    request.original_shader = producer;
    request.vertex_transform_kind = termin::VertexTransformKind::StaticMesh;
    request.pass_contract = pass;
    request.debug_context = "project-surface-contract-smoke";
    termin::TcShader variant = termin::assemble_material_shader_override(request);
    REQUIRE(variant.is_valid());
    REQUIRE(variant.is_executable());
    CHECK(std::strstr(variant.fragment_source(), "TestWeatheredSurfaceV1") != nullptr);
    CHECK(std::strstr(variant.fragment_source(), "surface.weathering") != nullptr);

    tc_shader_contract_view contract{};
    REQUIRE(tc_shader_get_contract_view(variant.get(), &contract));
    REQUIRE(contract_resource(contract, "weather_material") != nullptr);
    REQUIRE(contract_resource(contract, "weather_pass") != nullptr);
    REQUIRE(contract_resource(contract, "draw_data") != nullptr);
    REQUIRE(contract_resource(contract, TC_SHADER_RESOURCE_PER_FRAME) != nullptr);

    {
        ScopedArtifactConfiguration artifacts;
        std::vector<uint8_t> vertex_artifact;
        std::vector<uint8_t> fragment_artifact;
        REQUIRE(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
            variant.get(), tgfx::BackendType::Vulkan, tgfx::ShaderStage::Vertex, vertex_artifact));
        REQUIRE_FALSE(vertex_artifact.empty());
        REQUIRE(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
            variant.get(), tgfx::BackendType::Vulkan, tgfx::ShaderStage::Fragment, fragment_artifact));
        REQUIRE_FALSE(fragment_artifact.empty());
    }

    CHECK_EQ(termin_project_surface_test_plugin_unregister_owner(), 1u);
    CHECK(tc_surface_contract_registry_find(project_key) == nullptr);

    const tc_surface_contract_desc* standard_after = tc_surface_contract_registry_find(standard_key);
    REQUIRE(standard_after != nullptr);
    CHECK(std::string(standard_after->source_identity) == standard_identity);
    CHECK(std::string(standard_after->interface_source) == standard_interface);

    CHECK(std::strstr(variant.fragment_source(), "surface.weathering") != nullptr);
    termin::TcShader after_unload = termin::assemble_material_shader_override(request);
    CHECK_FALSE(after_unload.is_valid());

    tc_surface_contract_registry_clear();
    tc_shader_shutdown();
}
