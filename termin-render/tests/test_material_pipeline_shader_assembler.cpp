#include "guard_main.h"

GUARD_TEST_MAIN();

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include <termin/render/material_pipeline.hpp>
#include <termin/render/material_pipeline_shader_assembler.hpp>

namespace {

constexpr const char* kVertexSource = R"(
import termin_prelude;
struct VertexOutput { float4 position : SV_Position; };
[shader("vertex")]
VertexOutput vs_main() {
    VertexOutput output;
    output.position = float4(0.0, 0.0, 0.0, 1.0);
    return output;
}
)";

constexpr const char* kPositionVertexSource = R"(
import termin_prelude;
struct VertexInput { float3 position : POSITION; };
struct VertexOutput { float4 position : SV_Position; };
[shader("vertex")]
VertexOutput vs_main(VertexInput input) {
    VertexOutput output;
    output.position = float4(input.position, 1.0);
    return output;
}
)";

constexpr const char* kFragmentSource = R"(
struct FragmentOutput { float4 color : SV_Target0; };
[shader("fragment")]
FragmentOutput fs_main() {
    FragmentOutput output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    return output;
}
)";

constexpr const char* kStandardNormalFragmentSource = R"(
struct FragmentInput
{
    float4 screen_pos : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal_world : TEXCOORD1;
};

struct FragmentOutput { float4 color : SV_Target0; };

[shader("fragment")]
FragmentOutput fs_main(FragmentInput input)
{
    FragmentOutput output;
    float3 n = normalize(input.normal_world);
    output.color = float4(n * 0.5 + 0.5, 1.0);
    return output;
}
)";

constexpr const char* kTestSurfaceInterface = R"(
struct TerminTestSurface {
    float3 color;
};
)";

constexpr const char* kTestSurfaceEvaluator = R"(
struct FragmentInput {
    float4 screen_pos : SV_Position;
    float3 world_pos : TEXCOORD0;
    float3 normal_world : TEXCOORD1;
};

TerminTestSurface evaluate_test_surface(FragmentInput input) {
    TerminTestSurface surface;
    surface.color = abs(input.normal_world) + input.world_pos * 0.0;
    return surface;
}
)";

constexpr const char* kTestSurfaceConsumerA = R"(
struct FragmentOutput { float4 color : SV_Target0; };
[shader("fragment")]
FragmentOutput consume_test_surface_a(FragmentInput input) {
    TerminTestSurface surface = evaluate_test_surface(input);
    FragmentOutput output;
    output.color = float4(surface.color, 1.0);
    return output;
}
)";

constexpr const char* kTestSurfaceConsumerB = R"(
struct FragmentOutput { float4 color : SV_Target0; };
[shader("fragment")]
FragmentOutput consume_test_surface_b(FragmentInput input) {
    TerminTestSurface surface = evaluate_test_surface(input);
    FragmentOutput output;
    output.color = float4(surface.color.bgr, 1.0);
    return output;
}
)";

bool contract_has_vertex_input(
    const tc_shader_contract_view& view,
    const char* semantic)
{
    for (uint32_t i = 0; i < view.vertex_input_count; ++i) {
        if (std::strcmp(view.vertex_inputs[i].semantic, semantic) == 0) {
            return true;
        }
    }
    return false;
}

const tc_shader_resource_requirement* contract_resource(
    const tc_shader_contract_view& view,
    const char* name)
{
    for (uint32_t i = 0; i < view.resource_count; ++i) {
        if (std::strcmp(view.resources[i].name, name) == 0) {
            return &view.resources[i];
        }
    }
    return nullptr;
}

termin::MaterialPipelineMaterialContract material_contract()
{
    termin::TcShaderCreateInfo create_info{};
    create_info.sources.vertex = "";
    create_info.sources.fragment = kFragmentSource;
    create_info.sources.name = "assembler-material-fragment";
    create_info.sources.fragment_entry = "fs_main";
    create_info.language = TC_SHADER_LANGUAGE_SLANG;
    create_info.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
    termin::TcShader shader = termin::TcShader::from_sources(create_info);
    REQUIRE(shader.is_valid());
    tc_shader_set_feature(shader.get(), TC_SHADER_FEATURE_LIGHTING_UBO);

    tc_shader_resource_binding resources[2]{};
    std::snprintf(resources[0].name, sizeof(resources[0].name), "%s", TC_SHADER_RESOURCE_MATERIAL);
    resources[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    resources[0].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    resources[0].set = TC_SHADER_RESOURCE_SET_DEFAULT;
    resources[0].binding = 1;
    resources[0].stage_mask = TC_SHADER_STAGE_VERTEX | TC_SHADER_STAGE_FRAGMENT;
    resources[0].size = 64;

    std::snprintf(resources[1].name, sizeof(resources[1].name), "%s", "draw_data");
    resources[1].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    resources[1].scope = TC_SHADER_RESOURCE_SCOPE_DRAW;
    resources[1].set = TC_SHADER_RESOURCE_SET_DEFAULT;
    resources[1].binding = 24;
    resources[1].stage_mask = TC_SHADER_STAGE_VERTEX;
    resources[1].size = 64;

    tc_shader_set_resource_layout(shader.get(), resources, 2);

    return termin::material_pipeline_material_contract_from_shader(
        shader,
        termin::material_pipeline_standard_material_fragment_interface());
}

termin::MaterialPipelineMaterialContract material_contract_from_fragment(
    const char* fragment_source,
    const char* name)
{
    termin::TcShaderCreateInfo create_info{};
    create_info.sources.vertex = "";
    create_info.sources.fragment = fragment_source;
    create_info.sources.name = name;
    create_info.sources.fragment_entry = "fs_main";
    create_info.language = TC_SHADER_LANGUAGE_SLANG;
    create_info.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
    termin::TcShader shader = termin::TcShader::from_sources(create_info);
    REQUIRE(shader.is_valid());
    return termin::material_pipeline_material_contract_from_shader(
        shader,
        termin::material_pipeline_standard_material_fragment_interface());
}

termin::MaterialPipelinePassContract material_pass_contract();

void register_test_surface_contract(const char* source_identity)
{
    const tc_surface_contract_desc descriptor = {
        TC_SURFACE_CONTRACT_DESCRIPTOR_ABI_VERSION,
        {"test.surface.synthetic", 1u},
        "Synthetic test surface",
        "TerminTestSurface",
        kTestSurfaceInterface,
        source_identity,
    };
    REQUIRE(tc_surface_contract_registry_register(
        "termin-render-tests",
        &descriptor));
}

termin::MaterialPipelineMaterialContract surface_material_contract(
    const char* uuid = "assembler-test-surface-producer")
{
    tc_shader_fragment_input inputs[2]{};
    std::snprintf(
        inputs[0].semantic,
        sizeof(inputs[0].semantic),
        "%s",
        "world_pos");
    inputs[0].type = TC_SHADER_CONTRACT_VALUE_FLOAT3;
    std::snprintf(
        inputs[1].semantic,
        sizeof(inputs[1].semantic),
        "%s",
        "normal_world");
    inputs[1].type = TC_SHADER_CONTRACT_VALUE_FLOAT3;

    tc_shader_resource_requirement resources[1]{};
    std::snprintf(
        resources[0].name,
        sizeof(resources[0].name),
        "%s",
        TC_SHADER_RESOURCE_MATERIAL);
    resources[0].kind = TC_SHADER_RESOURCE_CONSTANT_BUFFER;
    resources[0].scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    resources[0].stage_mask = TC_SHADER_STAGE_FRAGMENT;
    resources[0].size = 32u;

    const tc_shader_surface_producer_desc producer = {
        TC_SHADER_SURFACE_PRODUCER_SCHEMA_VERSION,
        "test.surface.synthetic",
        1u,
        "TerminTestSurface",
        "evaluate_test_surface",
        kTestSurfaceEvaluator,
        "test.surface.synthetic@1:evaluator:v1",
        inputs,
        2u,
        resources,
        1u,
    };

    termin::TcShaderCreateInfo create_info{};
    create_info.sources.vertex = "";
    create_info.sources.fragment = kTestSurfaceEvaluator;
    create_info.sources.name = "assembler-test-surface-producer";
    create_info.sources.fragment_entry = "evaluate_test_surface";
    create_info.uuid = uuid;
    create_info.language = TC_SHADER_LANGUAGE_SLANG;
    create_info.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
    create_info.surface_producer = &producer;
    termin::TcShader shader = termin::TcShader::from_sources(create_info);
    REQUIRE(shader.is_valid());
    REQUIRE(shader.has_surface_producer());
    return termin::material_pipeline_material_contract_from_shader(shader);
}

termin::MaterialPipelinePassContract surface_consumer_pass(
    const char* debug_name,
    const char* source,
    const char* entry,
    const char* source_identity)
{
    termin::MaterialPipelinePassContract pass = material_pass_contract();
    pass.debug_name = debug_name;
    pass.fragment_composition =
        termin::MaterialFragmentComposition::SurfaceConsumer;
    termin::MaterialSurfaceConsumerContract consumer{};
    consumer.accepted_surface = {"test.surface.synthetic", 1u};
    consumer.consumer_source = source;
    consumer.fragment_entry = entry;
    consumer.source_identity = source_identity;
    consumer.required_fragment_input.semantics.push_back(
        {"world_pos", termin::MaterialPipelineValueType::Float3});
    consumer.resources.push_back({
        {
            std::string(debug_name) + "_output",
            TC_SHADER_RESOURCE_STORAGE_TEXTURE,
            TC_SHADER_RESOURCE_SCOPE_PASS,
            TC_SHADER_STAGE_FRAGMENT,
            0u,
        },
        termin::MaterialPipelineResourceOwner::Pass,
    });
    pass.surface_consumer = std::move(consumer);
    return pass;
}

termin::MaterialPipelinePassContract material_pass_contract()
{
    termin::MaterialPipelinePassContract contract;
    contract.debug_name = "assembler_material_pass";
    contract.required_material_fragment_input =
        termin::material_pipeline_standard_material_fragment_interface();
    contract.vertex_output_adapter =
        termin::material_pipeline_standard_material_vertex_output_adapter();
    contract.static_vertex_transform =
        termin::material_pipeline_make_static_mesh_vertex_transform_provider(
            "static",
            termin::MeshVertexTransformProfile::Material,
            "draw_data.u_model");
    contract.skinned_vertex_transform =
        termin::material_pipeline_make_skinned_mesh_vertex_transform_provider(
            "skinned",
            termin::MeshVertexTransformProfile::Material,
            "draw_data.u_model");
    contract.static_vertex_transform->resources.push_back(
        termin::material_pipeline_draw_resource_decl(
            "draw_data", TC_SHADER_STAGE_VERTEX, 64u));
    contract.skinned_vertex_transform->resources.push_back(
        termin::material_pipeline_draw_resource_decl(
            "draw_data", TC_SHADER_STAGE_VERTEX, 64u));
    contract.foliage_vertex_transform =
        termin::material_pipeline_make_foliage_material_vertex_transform_provider(
            "foliage");
    return contract;
}

termin::VertexOutputAdapter modular_shadow_adapter();

termin::MaterialPipelinePassContract compact_auxiliary_pass_contract()
{
    termin::MaterialPipelinePassContract contract;
    contract.debug_name = "assembler_compact_auxiliary_pass";
    contract.required_material_fragment_input = termin::MaterialFragmentInterface{};
    contract.vertex_output_adapter = modular_shadow_adapter();
    contract.static_vertex_transform =
        termin::material_pipeline_make_static_mesh_vertex_transform_provider(
            "static_compact",
            termin::MeshVertexTransformProfile::Position,
            "compact_draw.u_model");
    contract.skinned_vertex_transform =
        termin::material_pipeline_make_skinned_mesh_vertex_transform_provider(
            "skinned_compact",
            termin::MeshVertexTransformProfile::Position,
            "compact_draw.u_model");
    contract.static_vertex_transform->resources.push_back(
        termin::material_pipeline_draw_resource_decl(
            "compact_draw", TC_SHADER_STAGE_VERTEX, 64u));
    contract.skinned_vertex_transform->resources.push_back(
        termin::material_pipeline_draw_resource_decl(
            "compact_draw", TC_SHADER_STAGE_VERTEX, 64u));
    return contract;
}

termin::MaterialFragmentInterface world_position_interface()
{
    termin::MaterialFragmentInterface interface;
    interface.semantics.push_back(
        {"world_pos", termin::MaterialPipelineValueType::Float3});
    return interface;
}

termin::MaterialFragmentInterface world_position_normal_interface()
{
    termin::MaterialFragmentInterface interface = world_position_interface();
    interface.semantics.push_back(
        {"normal_world", termin::MaterialPipelineValueType::Float3});
    return interface;
}

termin::VertexOutputAdapter auxiliary_output_adapter(
    const char* module,
    const char* function,
    const char* draw_resource,
    termin::MaterialFragmentInterface consumed_world,
    uint32_t draw_size = 64u)
{
    termin::VertexOutputAdapter adapter;
    adapter.debug_name = std::string(module) + "_test";
    adapter.source_module = {
        module,
        std::string("builtin_shaders/") + module + ".slang"};
    adapter.output_type_name = "VertexOutput";
    adapter.output_function = function;
    adapter.consumed_world_semantics = std::move(consumed_world);
    adapter.produced_output_semantics.semantics.push_back(
        {"clip_position", termin::MaterialPipelineValueType::Float4});
    adapter.resources = termin::material_pipeline_pass_vertex_resources(
        draw_resource,
        draw_size);
    return adapter;
}

termin::VertexTransformProvider modular_shadow_provider(bool skinned)
{
    return skinned
        ? termin::material_pipeline_make_skinned_mesh_vertex_transform_provider(
              "skinned_shadow_provider",
              termin::MeshVertexTransformProfile::Position,
              "shadow_draw.u_model")
        : termin::material_pipeline_make_static_mesh_vertex_transform_provider(
              "static_shadow_provider",
              termin::MeshVertexTransformProfile::Position,
              "shadow_draw.u_model");
}

termin::VertexOutputAdapter modular_shadow_adapter()
{
    termin::VertexOutputAdapter adapter;
    adapter.debug_name = "shadow_adapter";
    adapter.source_module = {
        "termin_shadow_vertex_output_adapter",
        "builtin_shaders/termin_shadow_vertex_output_adapter.slang"};
    adapter.output_type_name = "VertexOutput";
    adapter.output_function = "termin_shadow_clip_output";
    adapter.consumed_world_semantics = world_position_interface();
    adapter.produced_output_semantics.semantics.push_back(
        {"clip_position", termin::MaterialPipelineValueType::Float4});
    adapter.resources.push_back(termin::material_pipeline_abi_resource_decl(
        termin::ShaderAbiResourceId::PerFrame,
        TC_SHADER_STAGE_VERTEX,
        termin::MaterialPipelineResourceOwner::Pass));
    return adapter;
}

termin::MaterialPipelinePassContract modular_shadow_pass_contract()
{
    termin::MaterialPipelinePassContract pass;
    pass.debug_name = "modular_shadow";
    pass.vertex_output_adapter = modular_shadow_adapter();
    pass.static_vertex_transform = modular_shadow_provider(false);
    pass.static_vertex_transform->resources.push_back(
        termin::material_pipeline_draw_resource_decl(
            "shadow_draw",
            TC_SHADER_STAGE_VERTEX,
            64u));
    pass.skinned_vertex_transform = modular_shadow_provider(true);
    pass.skinned_vertex_transform->resources.push_back(
        termin::material_pipeline_draw_resource_decl(
            "shadow_draw",
            TC_SHADER_STAGE_VERTEX,
            64u));
    pass.foliage_vertex_transform =
        termin::material_pipeline_make_foliage_vertex_transform_provider(
            "foliage_shadow_provider",
            termin::MeshVertexTransformProfile::Position);
    pass.foliage_vertex_transform->kind =
        termin::VertexTransformKind::FoliageShadow;
    std::erase_if(
        pass.foliage_vertex_transform->resources,
        [](const termin::MaterialPipelineResourceDecl& resource) {
            return resource.requirement.name == "per_frame";
        });
    pass.foliage_vertex_transform->source_module = {
        "termin_shadow_foliage_transform",
        "builtin_shaders/termin_shadow_foliage_transform.slang"};
    pass.foliage_vertex_transform->entry_input_declaration = R"(
struct VertexInput {
    float3 position : POSITION;
    uint instance_id : SV_InstanceID;
};)";
    pass.foliage_vertex_transform->adapter_input_expression =
        "termin_shadow_foliage_world_position(input.position, input.instance_id)";
    pass.foliage_vertex_transform->produced_world_semantics =
        world_position_interface();
    return pass;
}

} // namespace

TEST_CASE("material contract projects reflected resources to fragment stage") {
    tc_shader_init();

    termin::MaterialPipelineMaterialContract material = material_contract();

    REQUIRE(material.resources.size() == 1);
    CHECK(material.resources[0].requirement.name == TC_SHADER_RESOURCE_MATERIAL);
    CHECK_EQ(
        material.resources[0].requirement.stage_mask,
        static_cast<uint32_t>(TC_SHADER_STAGE_FRAGMENT));

    tc_shader_destroy(material.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("surface producer composes with distinct pass consumers") {
    tc_shader_init();
    tc_surface_contract_registry_clear();
    register_test_surface_contract("test.surface.synthetic@1:interface:v1");

    termin::MaterialPipelineMaterialContract material =
        surface_material_contract();
    REQUIRE(material.required_fragment_input.semantics.size() == 2u);
    REQUIRE(material.resources.size() == 1u);
    CHECK(material.resources[0].requirement.name == TC_SHADER_RESOURCE_MATERIAL);

    termin::MaterialPipelinePassContract pass_a = surface_consumer_pass(
        "surface_consumer_a",
        kTestSurfaceConsumerA,
        "consume_test_surface_a",
        "consumer:a:v1");
    termin::MaterialPipelinePassContract pass_b = surface_consumer_pass(
        "surface_consumer_b",
        kTestSurfaceConsumerB,
        "consume_test_surface_b",
        "consumer:b:v1");

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material;
    request.pass = pass_a;
    request.vertex_transform = *pass_a.static_vertex_transform;
    request.shader_name = "surface-consumer-a";
    request.shader_uuid = "surface-consumer-a";

    termin::MaterialPipelineShaderAssemblyResult result_a =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE(result_a.ok());
    REQUIRE(result_a.shader.is_executable());
    CHECK_FALSE(result_a.shader.has_surface_producer());
    CHECK(
        std::string(result_a.shader.fragment_source()).find(
            "struct TerminTestSurface") != std::string::npos);
    CHECK(
        std::string(result_a.shader.fragment_source()).find(
            "evaluate_test_surface") != std::string::npos);
    CHECK(
        std::string(result_a.shader.fragment_source()).find(
            "consume_test_surface_a") != std::string::npos);

    tc_shader_contract_view view_a{};
    REQUIRE(tc_shader_get_contract_view(result_a.shader.get(), &view_a));
    REQUIRE(contract_resource(view_a, TC_SHADER_RESOURCE_MATERIAL) != nullptr);
    REQUIRE(contract_resource(view_a, "surface_consumer_a_output") != nullptr);

    request.pass = pass_b;
    request.vertex_transform = *pass_b.static_vertex_transform;
    request.shader_name = "surface-consumer-b";
    request.shader_uuid = "surface-consumer-b";
    termin::MaterialPipelineShaderAssemblyResult result_b =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE(result_b.ok());
    CHECK(
        std::string(result_b.shader.fragment_source()).find(
            "consume_test_surface_b") != std::string::npos);
    CHECK(
        std::string(result_a.shader.source_hash()) !=
        std::string(result_b.shader.source_hash()));

    const std::string fingerprint_a =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_NONE,
            *pass_a.static_vertex_transform,
            pass_a);
    const std::string fingerprint_b =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_NONE,
            *pass_b.static_vertex_transform,
            pass_b);
    CHECK(fingerprint_a != fingerprint_b);

    termin::MaterialShaderOverrideRequest variant_request{};
    variant_request.original_shader = material.shader;
    variant_request.vertex_transform_kind =
        termin::VertexTransformKind::StaticMesh;
    variant_request.pass_contract = pass_a;
    variant_request.debug_context = "surface-consumer-variant-test";
    termin::TcShader variant_a =
        termin::assemble_material_shader_override(variant_request);
    REQUIRE(variant_a.is_valid());
    REQUIRE(variant_a.is_executable());
    termin::TcShader cached_a =
        termin::assemble_material_shader_override(variant_request);
    REQUIRE(cached_a.is_valid());
    CHECK(tc_shader_handle_eq(variant_a.handle, cached_a.handle));

    variant_request.pass_contract = pass_b;
    termin::TcShader variant_b =
        termin::assemble_material_shader_override(variant_request);
    REQUIRE(variant_b.is_valid());
    CHECK_FALSE(tc_shader_handle_eq(variant_a.handle, variant_b.handle));

    tc_surface_contract_registry_clear();
    tc_shader_shutdown();
}

TEST_CASE("hybrid color composition keeps final-color and surface materials compatible") {
    tc_shader_init();
    tc_surface_contract_registry_clear();
    register_test_surface_contract("test.surface.synthetic@1:interface:v1");

    termin::MaterialPipelinePassContract pass = surface_consumer_pass(
        "hybrid_color",
        kTestSurfaceConsumerA,
        "consume_test_surface_a",
        "consumer:a:v1");
    pass.fragment_composition =
        termin::MaterialFragmentComposition::SurfaceConsumerOrFinalColor;

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.pass = pass;
    request.vertex_transform = *pass.static_vertex_transform;

    termin::MaterialPipelineMaterialContract final_material =
        material_contract();
    request.material = final_material;
    request.shader_name = "hybrid-final-color";
    request.shader_uuid = "hybrid-final-color";
    termin::MaterialPipelineShaderAssemblyResult final_color =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE(final_color.ok());
    CHECK(
        std::string(final_color.shader.fragment_source()).find("fs_main") !=
        std::string::npos);

    termin::MaterialPipelineMaterialContract surface_material =
        surface_material_contract(
            "hybrid-surface-producer");
    request.material = surface_material;
    request.shader_name = "hybrid-surface";
    request.shader_uuid = "hybrid-surface";
    termin::MaterialPipelineShaderAssemblyResult surface =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE(surface.ok());
    CHECK(
        std::string(surface.shader.fragment_source()).find(
            "consume_test_surface_a") != std::string::npos);

    termin::MaterialPipelinePassContract explicit_pass = pass;
    explicit_pass.fragment_composition =
        termin::MaterialFragmentComposition::FinalColor;
    CHECK(
        termin::material_pipeline_shader_intent_fingerprint(
            final_material.shader,
            TC_SHADER_VARIANT_NONE,
            *pass.static_vertex_transform,
            pass) ==
        termin::material_pipeline_shader_intent_fingerprint(
            final_material.shader,
            TC_SHADER_VARIANT_NONE,
            *explicit_pass.static_vertex_transform,
            explicit_pass));

    explicit_pass.fragment_composition =
        termin::MaterialFragmentComposition::SurfaceConsumer;
    CHECK(
        termin::material_pipeline_shader_intent_fingerprint(
            surface_material.shader,
            TC_SHADER_VARIANT_NONE,
            *pass.static_vertex_transform,
            pass) ==
        termin::material_pipeline_shader_intent_fingerprint(
            surface_material.shader,
            TC_SHADER_VARIANT_NONE,
            *explicit_pass.static_vertex_transform,
            explicit_pass));

    tc_surface_contract_registry_clear();
    tc_shader_shutdown();
}

TEST_CASE("surface composition rejects incompatible fragment roles and contracts") {
    tc_shader_init();
    tc_surface_contract_registry_clear();
    register_test_surface_contract("test.surface.synthetic@1:interface:v1");

    termin::MaterialPipelineMaterialContract surface =
        surface_material_contract();
    termin::MaterialPipelineMaterialContract final_color = material_contract();
    termin::MaterialPipelinePassContract surface_pass = surface_consumer_pass(
        "surface_consumer",
        kTestSurfaceConsumerA,
        "consume_test_surface_a",
        "consumer:a:v1");

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = final_color;
    request.pass = surface_pass;
    request.vertex_transform = *surface_pass.static_vertex_transform;
    request.shader_name = "final-color-as-surface";
    request.shader_uuid = "final-color-as-surface";
    termin::MaterialPipelineShaderAssemblyResult final_as_surface =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE_FALSE(final_as_surface.ok());
    REQUIRE_FALSE(final_as_surface.diagnostics.empty());
    CHECK(
        final_as_surface.diagnostics[0].code ==
        termin::MaterialPipelineDiagnosticCode::FragmentCompositionMismatch);

    request.material = surface;
    request.pass = material_pass_contract();
    request.vertex_transform = *request.pass.static_vertex_transform;
    request.shader_name = "surface-as-final-color";
    request.shader_uuid = "surface-as-final-color";
    termin::MaterialPipelineShaderAssemblyResult surface_as_final =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE_FALSE(surface_as_final.ok());
    REQUIRE_FALSE(surface_as_final.diagnostics.empty());
    CHECK(
        surface_as_final.diagnostics[0].code ==
        termin::MaterialPipelineDiagnosticCode::FragmentCompositionMismatch);

    request.pass = surface_pass;
    request.pass.surface_consumer->accepted_surface.version = 2u;
    request.vertex_transform = *request.pass.static_vertex_transform;
    request.shader_name = "surface-contract-mismatch";
    request.shader_uuid = "surface-contract-mismatch";
    termin::MaterialPipelineShaderAssemblyResult mismatch =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE_FALSE(mismatch.diagnostics.empty());
    CHECK(
        mismatch.diagnostics[0].code ==
        termin::MaterialPipelineDiagnosticCode::SurfaceContractMismatch);

    request.pass = surface_pass;
    request.pass.surface_consumer->fragment_entry = "missing_consumer_entry";
    request.shader_name = "surface-missing-consumer";
    request.shader_uuid = "surface-missing-consumer";
    termin::MaterialPipelineShaderAssemblyResult missing_entry =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE_FALSE(missing_entry.ok());
    CHECK(std::any_of(
        missing_entry.diagnostics.begin(),
        missing_entry.diagnostics.end(),
        [](const termin::MaterialPipelineDiagnostic& item) {
            return item.code ==
                termin::MaterialPipelineDiagnosticCode::MissingFragmentEntry;
        }));

    request.pass = surface_pass;
    request.vertex_transform =
        termin::material_pipeline_make_static_mesh_vertex_transform_provider(
            "surface_position_only",
            termin::MeshVertexTransformProfile::Position,
            "draw_data.u_model");
    request.pass.vertex_output_adapter =
        modular_shadow_adapter();
    request.shader_name = "surface-missing-semantic";
    request.shader_uuid = "surface-missing-semantic";
    termin::MaterialPipelineShaderAssemblyResult missing_semantic =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE_FALSE(missing_semantic.ok());
    CHECK(std::any_of(
        missing_semantic.diagnostics.begin(),
        missing_semantic.diagnostics.end(),
        [](const termin::MaterialPipelineDiagnostic& item) {
            return item.code ==
                termin::MaterialPipelineDiagnosticCode::MissingVertexOutputSemantic;
        }));

    tc_surface_contract_registry_clear();
    tc_shader_shutdown();
}

TEST_CASE("surface composition preserves static skinned and foliage resources") {
    tc_shader_init();
    tc_surface_contract_registry_clear();
    register_test_surface_contract("test.surface.synthetic@1:interface:v1");

    termin::MaterialPipelineMaterialContract material =
        surface_material_contract();
    termin::MaterialPipelinePassContract pass = surface_consumer_pass(
        "surface_transform_variants",
        kTestSurfaceConsumerA,
        "consume_test_surface_a",
        "consumer:a:v1");

    struct TransformCase {
        const char* name;
        const termin::VertexTransformContract* transform;
        const char* expected_resource;
    };
    const TransformCase cases[] = {
        {"static", &*pass.static_vertex_transform, "draw_data"},
        {"skinned", &*pass.skinned_vertex_transform, TC_SHADER_RESOURCE_BONE_BLOCK},
        {"foliage", &*pass.foliage_vertex_transform, "foliage_instances"},
    };

    for (const TransformCase& transform_case : cases) {
        termin::MaterialPipelineShaderAssemblyRequest request{};
        request.material = material;
        request.pass = pass;
        request.vertex_transform = *transform_case.transform;
        request.shader_name =
            std::string("surface-transform-") + transform_case.name;
        request.shader_uuid =
            std::string("surface-transform-") + transform_case.name;
        termin::MaterialPipelineShaderAssemblyResult result =
            termin::material_pipeline_assemble_shader(request);
        REQUIRE(result.ok());
        tc_shader_contract_view view{};
        REQUIRE(tc_shader_get_contract_view(result.shader.get(), &view));
        REQUIRE(contract_resource(view, TC_SHADER_RESOURCE_MATERIAL) != nullptr);
        REQUIRE(contract_resource(view, transform_case.expected_resource) != nullptr);
        CHECK(contract_has_vertex_input(view, "position"));
    }

    tc_surface_contract_registry_clear();
    tc_shader_shutdown();
}

TEST_CASE("surface fingerprint tracks producer consumer and interface identities") {
    tc_shader_init();
    tc_surface_contract_registry_clear();
    register_test_surface_contract("test.surface.synthetic@1:interface:v1");

    termin::MaterialPipelineMaterialContract material =
        surface_material_contract();
    termin::MaterialPipelinePassContract pass = surface_consumer_pass(
        "surface_identity",
        kTestSurfaceConsumerA,
        "consume_test_surface_a",
        "consumer:a:v1");
    const termin::VertexTransformContract& transform =
        *pass.static_vertex_transform;
    const std::string original =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_NONE,
            transform,
            pass);

    termin::MaterialPipelinePassContract changed_consumer = pass;
    changed_consumer.surface_consumer->source_identity = "consumer:a:v2";
    CHECK(
        original != termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_NONE,
            transform,
            changed_consumer));

    REQUIRE(tc_surface_contract_registry_unregister(
        "termin-render-tests",
        {"test.surface.synthetic", 1u}));
    register_test_surface_contract("test.surface.synthetic@1:interface:v2");
    const std::string changed_interface =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_NONE,
            transform,
            pass);
    CHECK(original != changed_interface);

    tc_shader_surface_producer_view producer{};
    REQUIRE(tc_shader_get_surface_producer_view(material.shader.get(), &producer));
    tc_shader_surface_producer_desc changed_producer = {
        producer.schema_version,
        producer.contract_id,
        producer.contract_version,
        producer.surface_type_name,
        producer.evaluator_entry,
        producer.evaluator_source,
        "test.surface.synthetic@1:evaluator:v2",
        producer.fragment_inputs,
        producer.fragment_input_count,
        producer.resources,
        producer.resource_count,
    };
    REQUIRE(tc_shader_set_surface_producer(
        material.shader.get(),
        &changed_producer));
    const std::string changed_all =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_NONE,
            transform,
            pass);
    CHECK(changed_interface != changed_all);

    tc_surface_contract_registry_clear();
    tc_shader_shutdown();
}

TEST_CASE("pass-owned fragment is explicit and ignores material resources") {
    tc_shader_init();

    termin::MaterialPipelinePassContract pass = material_pass_contract();
    pass.debug_name = "pass_owned";
    pass.fragment_composition =
        termin::MaterialFragmentComposition::PassOwned;
    pass.fragment_source_override = kFragmentSource;
    pass.fragment_entry_override = "fs_main";
    pass.required_material_fragment_input = {};

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.pass = pass;
    request.vertex_transform = *pass.static_vertex_transform;
    request.shader_name = "pass-owned-fragment";
    request.shader_uuid = "pass-owned-fragment";
    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE(result.ok());
    CHECK(std::string(result.shader.fragment_source()) == kFragmentSource);
    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(result.shader.get(), &view));
    CHECK(contract_resource(view, TC_SHADER_RESOURCE_MATERIAL) == nullptr);

    tc_shader_shutdown();
}

TEST_CASE("material pipeline assembler attaches skinned shader contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.pass = material_pass_contract();
    request.vertex_transform = *request.pass.skinned_vertex_transform;
    request.shader_name = "assembler-skinned-contract";
    request.shader_uuid = "assembler-skinned-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    REQUIRE(tc_shader_has_feature(result.shader.get(), TC_SHADER_FEATURE_LIGHTING_UBO));
    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(result.shader.get(), &view));
    CHECK_EQ(view.source_kind, TC_SHADER_CONTRACT_SOURCE_ASSEMBLED);
    CHECK(contract_has_vertex_input(view, "position"));
    CHECK(contract_has_vertex_input(view, "joints"));
    CHECK(contract_has_vertex_input(view, "weights"));

    const tc_shader_resource_requirement* bone =
        contract_resource(view, TC_SHADER_RESOURCE_BONE_BLOCK);
    REQUIRE(bone != nullptr);
    CHECK_EQ(bone->scope, TC_SHADER_RESOURCE_SCOPE_DRAW);
    CHECK(!tc_shader_has_resource_layout(result.shader.get()));
    CHECK(tc_shader_find_resource_binding(
              result.shader.get(),
              TC_SHADER_RESOURCE_BONE_BLOCK) == nullptr);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material pipeline assembler keeps skinned debug normal material semantics linkable") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract_from_fragment(
        kStandardNormalFragmentSource,
        "assembler-standard-normal-fragment");
    request.pass = material_pass_contract();
    request.vertex_transform = *request.pass.skinned_vertex_transform;
    request.shader_name = "assembler-skinned-standard-normal";
    request.shader_uuid = "assembler-skinned-standard-normal";

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    REQUIRE(result.shader.get() != nullptr);
    const std::string vertex_source = result.shader.vertex_source();
    const std::string fragment_source = result.shader.fragment_source();
    CHECK(vertex_source.find(
              "import termin_material_vertex_output_adapter;") != std::string::npos);
    CHECK(vertex_source.find("termin_skinned_world_vertex") != std::string::npos);
    CHECK(fragment_source.find("normal_world : TEXCOORD1") != std::string::npos);
    CHECK(fragment_source.find("normal_world : NORMAL") == std::string::npos);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material pipeline assembler attaches foliage instance contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.pass = material_pass_contract();
    request.vertex_transform = *request.pass.foliage_vertex_transform;
    request.shader_name = "assembler-foliage-contract";
    request.shader_uuid = "assembler-foliage-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(result.shader.get(), &view));
    CHECK(contract_has_vertex_input(view, "position"));
    CHECK(contract_has_vertex_input(view, "normal"));
    CHECK(contract_has_vertex_input(view, "uv"));

    const tc_shader_resource_requirement* instances =
        contract_resource(view, "foliage_instances");
    REQUIRE(instances != nullptr);
    CHECK_EQ(instances->kind, TC_SHADER_RESOURCE_STORAGE_BUFFER);
    CHECK_EQ(instances->element_stride, 32u);
    CHECK(!tc_shader_has_resource_layout(result.shader.get()));
    CHECK(tc_shader_find_resource_binding(
              result.shader.get(),
              "foliage_instances") == nullptr);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material mesh input selection follows static compact shader contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.material.required_fragment_input = {};
    request.pass = compact_auxiliary_pass_contract();
    request.vertex_transform = *request.pass.static_vertex_transform;
    request.shader_name = "assembler-static-shadow-contract";
    request.shader_uuid = "assembler-static-shadow-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    CHECK(
        termin::material_mesh_vertex_input_for_shader(
            result.shader.get(),
            termin::MaterialMeshVertexInput::FullMaterial) ==
        termin::MaterialMeshVertexInput::Position);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material mesh input selection follows skinned compact shader contract") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.material.required_fragment_input = {};
    request.pass = compact_auxiliary_pass_contract();
    request.vertex_transform = *request.pass.skinned_vertex_transform;
    request.shader_name = "assembler-skinned-shadow-contract";
    request.shader_uuid = "assembler-skinned-shadow-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    CHECK(
        termin::material_mesh_vertex_input_for_shader(
            result.shader.get(),
            termin::MaterialMeshVertexInput::FullMaterial) ==
        termin::MaterialMeshVertexInput::SkinnedPositionJointsWeights);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material mesh input selection keeps full skinned material attributes") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.pass = material_pass_contract();
    request.vertex_transform = *request.pass.skinned_vertex_transform;
    request.shader_name = "assembler-skinned-full-material-contract";
    request.shader_uuid = "assembler-skinned-full-material-contract";
    request.vertex_source_override = kVertexSource;

    termin::MaterialPipelineShaderAssemblyResult result =
        termin::material_pipeline_assemble_shader(request);

    REQUIRE(result.ok());
    CHECK(
        termin::material_mesh_vertex_input_for_shader(
            result.shader.get(),
            termin::MaterialMeshVertexInput::FullMaterial) ==
        termin::MaterialMeshVertexInput::SkinnedFullMaterial);

    tc_shader_destroy(result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material pipeline composes one skinned provider across pass adapters") {
    struct AdapterCase {
        const char* name;
        const char* module;
        const char* function;
        const char* draw_resource;
        const char* model_expression;
        termin::MeshVertexTransformProfile profile;
        bool material;
        bool normal_input;
        uint32_t draw_size;
    };
    const AdapterCase cases[] = {
        {"material", "termin_material_vertex_output_adapter",
         "termin_material_vertex_output", "draw_data", "draw_data.u_model",
         termin::MeshVertexTransformProfile::Material, true, true, 64u},
        {"depth", "termin_depth_vertex_output_adapter",
         "termin_depth_vertex_output", "depth_draw", "depth_draw.u_model",
         termin::MeshVertexTransformProfile::Position, false, false, 64u},
        {"id", "termin_id_vertex_output_adapter",
         "termin_id_vertex_output", "id_model", "id_model.model",
         termin::MeshVertexTransformProfile::Position, false, false, 64u},
        {"normal", "termin_normal_vertex_output_adapter",
         "termin_normal_vertex_output", "normal_draw", "normal_draw.u_model",
         termin::MeshVertexTransformProfile::PositionNormal, false, true, 64u},
    };

    tc_shader_init();
    for (const AdapterCase& adapter_case : cases) {
        termin::MaterialPipelinePassContract pass;
        pass.debug_name = std::string("modular_") + adapter_case.name;
        pass.vertex_output_adapter = adapter_case.material
            ? termin::material_pipeline_standard_material_vertex_output_adapter()
            : auxiliary_output_adapter(
                  adapter_case.module,
                  adapter_case.function,
                  adapter_case.draw_resource,
                  adapter_case.normal_input
                      ? world_position_normal_interface()
                      : world_position_interface(),
                  adapter_case.draw_size);
        pass.skinned_vertex_transform =
            termin::material_pipeline_make_skinned_mesh_vertex_transform_provider(
                std::string("skinned_") + adapter_case.name,
                adapter_case.profile,
                adapter_case.model_expression);
        if (adapter_case.material) {
            pass.skinned_vertex_transform->resources.push_back(
                termin::material_pipeline_draw_resource_decl(
                    adapter_case.draw_resource,
                    TC_SHADER_STAGE_VERTEX,
                    adapter_case.draw_size));
        }

        termin::MaterialPipelineShaderAssemblyRequest request{};
        request.material = material_contract();
        if (!adapter_case.material) {
            request.material.required_fragment_input = {};
        }
        request.pass = pass;
        request.vertex_transform = *pass.skinned_vertex_transform;
        request.shader_name = std::string("adapter-") + adapter_case.name;
        request.shader_uuid = std::string("adapter-") + adapter_case.name;

        termin::MaterialPipelineShaderAssemblyResult result =
            termin::material_pipeline_assemble_shader(request);
        REQUIRE(result.ok());
        const std::string source = result.shader.vertex_source();
        CHECK(source.find("import termin_vertex_transform;") != std::string::npos);
        CHECK(source.find(
                  std::string("import ") + adapter_case.module + ";") !=
              std::string::npos);
        CHECK(source.find("termin_skinned_") != std::string::npos);
        CHECK(source.find("template_uuid") == std::string::npos);
        if (std::string_view(adapter_case.name) == "id") {
            CHECK(source.find("id_model.model") != std::string::npos);
        }

        tc_shader_contract_view view{};
        REQUIRE(tc_shader_get_contract_view(result.shader.get(), &view));
        CHECK(contract_has_vertex_input(view, "joints"));
        CHECK(contract_has_vertex_input(view, "normal") ==
              adapter_case.normal_input);
        REQUIRE(contract_resource(view, TC_SHADER_RESOURCE_BONE_BLOCK) != nullptr);
        REQUIRE(contract_resource(view, adapter_case.draw_resource) != nullptr);
        tc_shader_destroy(result.shader.handle);
    }
    tc_shader_shutdown();
}

TEST_CASE("material pipeline assembler composes shadow providers with one output adapter") {
    tc_shader_init();

    termin::MaterialPipelineShaderAssemblyRequest request{};
    request.material = material_contract();
    request.material.required_fragment_input = {};
    request.pass = modular_shadow_pass_contract();
    request.vertex_transform = *request.pass.static_vertex_transform;
    request.shader_name = "assembler-modular-static-shadow";
    request.shader_uuid = "assembler-modular-static-shadow";

    termin::MaterialPipelineShaderAssemblyResult static_result =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE(static_result.ok());
    const std::string static_source = static_result.shader.vertex_source();
    CHECK(static_source.find(
              "import termin_vertex_transform;") != std::string::npos);
    CHECK(static_source.find(
              "import termin_shadow_vertex_output_adapter;") != std::string::npos);
    CHECK(static_source.find(
              "termin_static_world_position") != std::string::npos);

    tc_shader_contract_view static_view{};
    REQUIRE(tc_shader_get_contract_view(static_result.shader.get(), &static_view));
    REQUIRE(contract_resource(static_view, "per_frame") != nullptr);
    REQUIRE(contract_resource(static_view, "shadow_draw") != nullptr);
    CHECK(contract_resource(static_view, TC_SHADER_RESOURCE_BONE_BLOCK) == nullptr);

    request.vertex_transform = *request.pass.skinned_vertex_transform;
    request.shader_name = "assembler-modular-skinned-shadow";
    request.shader_uuid = "assembler-modular-skinned-shadow";
    termin::MaterialPipelineShaderAssemblyResult skinned_result =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE(skinned_result.ok());
    const std::string skinned_source = skinned_result.shader.vertex_source();
    CHECK(skinned_source.find(
              "import termin_vertex_transform;") != std::string::npos);
    CHECK(skinned_source.find(
              "termin_skinned_world_position") != std::string::npos);

    tc_shader_contract_view skinned_view{};
    REQUIRE(tc_shader_get_contract_view(skinned_result.shader.get(), &skinned_view));
    const tc_shader_resource_requirement* bone =
        contract_resource(skinned_view, TC_SHADER_RESOURCE_BONE_BLOCK);
    REQUIRE(bone != nullptr);
    CHECK_EQ(bone->scope, TC_SHADER_RESOURCE_SCOPE_DRAW);

    request.vertex_transform = *request.pass.foliage_vertex_transform;
    request.shader_name = "assembler-modular-foliage-shadow";
    request.shader_uuid = "assembler-modular-foliage-shadow";
    termin::MaterialPipelineShaderAssemblyResult foliage_result =
        termin::material_pipeline_assemble_shader(request);
    REQUIRE(foliage_result.ok());
    const std::string foliage_source = foliage_result.shader.vertex_source();
    CHECK(foliage_source.find(
              "import termin_shadow_foliage_transform;") != std::string::npos);
    CHECK(foliage_source.find(
              "termin_shadow_foliage_world_position(input.position, input.instance_id)") !=
          std::string::npos);
    CHECK(foliage_source.find("instance_id : SV_InstanceID") != std::string::npos);

    tc_shader_contract_view foliage_view{};
    REQUIRE(tc_shader_get_contract_view(foliage_result.shader.get(), &foliage_view));
    REQUIRE(contract_resource(foliage_view, "per_frame") != nullptr);
    CHECK(contract_resource(foliage_view, "shadow_draw") == nullptr);
    REQUIRE(contract_resource(foliage_view, "foliage_draw") != nullptr);
    const tc_shader_resource_requirement* instances =
        contract_resource(foliage_view, "foliage_instances");
    REQUIRE(instances != nullptr);
    CHECK_EQ(instances->scope, TC_SHADER_RESOURCE_SCOPE_DRAW);
    CHECK_EQ(instances->element_stride, 32u);

    tc_shader_destroy(static_result.shader.handle);
    tc_shader_destroy(skinned_result.shader.handle);
    tc_shader_destroy(foliage_result.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material shader intent fingerprint includes skinned vertex transform") {
    tc_shader_init();

    termin::MaterialPipelineMaterialContract material = material_contract();
    termin::MaterialPipelinePassContract pass_a = compact_auxiliary_pass_contract();
    termin::MaterialPipelinePassContract pass_b = pass_a;
    pass_b.skinned_vertex_transform->adapter_input_expression += " ";

    const std::string fingerprint_a =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_SKINNING,
            *pass_a.skinned_vertex_transform,
            pass_a);
    const std::string fingerprint_b =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_SKINNING,
            *pass_b.skinned_vertex_transform,
            pass_b);

    CHECK(fingerprint_a != fingerprint_b);

    tc_shader_destroy(material.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material shader intent fingerprint includes modular source identities") {
    tc_shader_init();

    termin::MaterialPipelineMaterialContract material = material_contract();
    termin::MaterialPipelinePassContract pass_a = modular_shadow_pass_contract();
    termin::MaterialPipelinePassContract pass_b = pass_a;
    pass_b.vertex_output_adapter->source_module.source_identity =
        "builtin_shaders/termin_shadow_vertex_output_adapter.changed.slang";

    const std::string fingerprint_a =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_NONE,
            *pass_a.static_vertex_transform,
            pass_a);
    const std::string fingerprint_b =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_NONE,
            *pass_b.static_vertex_transform,
            pass_b);

    CHECK(fingerprint_a != fingerprint_b);

    termin::VertexTransformContract changed_provider =
        *pass_a.static_vertex_transform;
    changed_provider.source_module.source_identity =
        "builtin_shaders/termin_vertex_transform.changed.slang";
    const std::string provider_fingerprint =
        termin::material_pipeline_shader_intent_fingerprint(
            material.shader,
            TC_SHADER_VARIANT_NONE,
            changed_provider,
            pass_a);

    CHECK(fingerprint_a != provider_fingerprint);

    tc_shader_destroy(material.shader.handle);
    tc_shader_shutdown();
}

TEST_CASE("material shader overrides stay canonical across frame-local owners") {
    tc_shader_init();

    termin::MaterialPipelineMaterialContract material = material_contract();
    termin::MaterialPipelinePassContract pass = compact_auxiliary_pass_contract();
    termin::MaterialShaderOverrideRequest request{};
    request.original_shader = material.shader;
    request.vertex_transform_kind = termin::VertexTransformKind::SkinnedMesh;
    request.pass_contract = pass;
    request.shader_variant_op = TC_SHADER_VARIANT_SKINNING;
    request.debug_context = "canonical-override-test";

    tc_shader_handle first_handle = tc_shader_handle_invalid();
    {
        termin::TcShader first = termin::assemble_material_shader_override(request);
        REQUIRE(first.is_valid());
        first_handle = first.handle;
        REQUIRE(first.get() != nullptr);
        CHECK(first.get()->is_static != 0);
        CHECK_FALSE(tc_shader_variant_is_stale(first_handle));
    }

    // The render task was the last temporary owner, but registry ownership
    // keeps the derived shader and its device-handle identity alive.
    REQUIRE(tc_shader_is_valid(first_handle));
    termin::TcShader second = termin::assemble_material_shader_override(request);
    REQUIRE(second.is_valid());
    CHECK(tc_shader_handle_eq(second.handle, first_handle));

    // Hot reload/version changes must bypass the fast path and refresh the
    // canonical variant metadata rather than returning stale shader state.
    tc_shader_bump_version(material.shader.get());
    CHECK(tc_shader_variant_is_stale(first_handle));
    termin::TcShader refreshed = termin::assemble_material_shader_override(request);
    REQUIRE(refreshed.is_valid());
    CHECK(tc_shader_handle_eq(refreshed.handle, first_handle));
    CHECK_FALSE(tc_shader_variant_is_stale(refreshed.handle));

    tc_shader_shutdown();
}

TEST_CASE("static material shader overrides are canonical derived shaders") {
    tc_shader_init();

    termin::MaterialPipelineMaterialContract material = material_contract();
    termin::MaterialPipelinePassContract pass = material_pass_contract();
    termin::MaterialShaderOverrideRequest request{};
    request.original_shader = material.shader;
    request.vertex_transform_kind = termin::VertexTransformKind::StaticMesh;
    request.pass_contract = pass;
    request.debug_context = "canonical-static-override-test";

    termin::TcShader first = termin::assemble_material_shader_override(request);
    REQUIRE(first.is_valid());
    REQUIRE(first.get() != nullptr);
    CHECK_FALSE(tc_shader_handle_eq(first.handle, material.shader.handle));
    CHECK(first.is_variant());
    CHECK(first.variant_op() == TC_SHADER_VARIANT_NONE);
    CHECK(tc_shader_handle_eq(first.original().handle, material.shader.handle));

    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(first.get(), &view));
    const tc_shader_resource_requirement* material_resource =
        contract_resource(view, TC_SHADER_RESOURCE_MATERIAL);
    REQUIRE(material_resource != nullptr);
    CHECK_EQ(
        material_resource->stage_mask,
        static_cast<uint32_t>(TC_SHADER_STAGE_FRAGMENT));

    const tc_shader_handle first_handle = first.handle;
    first = termin::TcShader();
    termin::TcShader second = termin::assemble_material_shader_override(request);
    REQUIRE(second.is_valid());
    CHECK(tc_shader_handle_eq(second.handle, first_handle));

    tc_shader_bump_version(material.shader.get());
    CHECK(tc_shader_variant_is_stale(second.handle));
    termin::TcShader refreshed = termin::assemble_material_shader_override(request);
    REQUIRE(refreshed.is_valid());
    CHECK(tc_shader_handle_eq(refreshed.handle, first_handle));
    CHECK_FALSE(tc_shader_variant_is_stale(refreshed.handle));

    tc_shader_shutdown();
}

TEST_CASE("static authored vertex interface is not widened by material pipeline planning") {
    tc_shader_init();

    termin::TcShaderCreateInfo create_info{};
    create_info.sources.vertex = kPositionVertexSource;
    create_info.sources.fragment = kFragmentSource;
    create_info.sources.name = "authored-static-position-material";
    create_info.sources.vertex_entry = "vs_main";
    create_info.sources.fragment_entry = "fs_main";
    create_info.language = TC_SHADER_LANGUAGE_SLANG;
    create_info.artifact_policy = TC_SHADER_ARTIFACT_REQUIRED;
    termin::TcShader shader = termin::TcShader::from_sources(create_info);
    REQUIRE(shader.is_valid());

    tc_shader_contract_vertex_input position{};
    std::snprintf(position.semantic, sizeof(position.semantic), "%s", "position");
    position.type = TC_SHADER_CONTRACT_VALUE_FLOAT3;
    position.required = 1;
    tc_shader_contract_desc contract{};
    contract.source_kind = TC_SHADER_CONTRACT_SOURCE_DECLARED;
    contract.vertex_inputs = &position;
    contract.vertex_input_count = 1;
    contract.debug_name = "authored-static-position-material";
    REQUIRE(tc_shader_set_contract(shader.get(), &contract));

    termin::MaterialShaderOverrideRequest request{};
    request.original_shader = shader;
    request.vertex_transform_kind = termin::VertexTransformKind::StaticMesh;
    request.pass_contract = material_pass_contract();
    request.debug_context = "authored-static-position-test";

    termin::TcShader planned = termin::assemble_material_shader_override(request);
    REQUIRE(planned.is_valid());
    CHECK(tc_shader_handle_eq(planned.handle, shader.handle));
    CHECK_FALSE(planned.is_variant());
    CHECK(
        termin::material_mesh_vertex_input_for_shader(
            planned.get(),
            termin::MaterialMeshVertexInput::FullMaterial) ==
        termin::MaterialMeshVertexInput::Position);

    tc_shader_contract_view view{};
    REQUIRE(tc_shader_get_contract_view(planned.get(), &view));
    CHECK(contract_has_vertex_input(view, "position"));
    CHECK_FALSE(contract_has_vertex_input(view, "normal"));
    CHECK_FALSE(contract_has_vertex_input(view, "uv"));
    CHECK_FALSE(contract_has_vertex_input(view, "tangent"));

    tc_shader_shutdown();
}
