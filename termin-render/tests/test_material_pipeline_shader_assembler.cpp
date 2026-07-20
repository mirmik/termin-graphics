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

termin::MaterialPipelinePassContract material_pass_contract()
{
    termin::MaterialPipelinePassContract contract;
    contract.debug_name = "assembler_material_pass";
    contract.required_material_fragment_input =
        termin::material_pipeline_standard_material_fragment_interface();
    contract.uses_material_fragment = true;
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
    contract.uses_material_fragment = true;
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
    pass.uses_material_fragment = true;
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
        termin::material_pipeline_make_foliage_vertex_transform_contract(
            termin::VertexTransformKind::FoliageShadow,
            "foliage_shadow_provider",
            "termin-engine-foliage-shadow",
            termin::material_pipeline_position_mesh_input(),
            termin::MaterialFragmentInterface{},
            termin::material_pipeline_foliage_vertex_resources());
    pass.foliage_vertex_transform->template_uuid.reset();
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
        pass.uses_material_fragment = true;
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
