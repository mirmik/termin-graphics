#include "guard_main.h"

GUARD_TEST_MAIN();

#include <array>
#include <vector>

#include <termin/render/material_pipeline_contracts.hpp>

namespace {

termin::MaterialPipelineResourceDecl resource(
    const char* name,
    uint32_t kind,
    uint32_t scope,
    uint32_t stage_mask,
    termin::MaterialPipelineResourceOwner owner)
{
    termin::MaterialPipelineResourceDecl result{};
    result.requirement.name = name;
    result.requirement.kind = kind;
    result.requirement.scope = scope;
    result.requirement.stage_mask = stage_mask;
    result.owner = owner;
    return result;
}

} // namespace

TEST_CASE("Material pipeline resource merge combines shared declarations") {
    std::array<termin::MaterialPipelineResourceDecl, 2> resources{{
        resource(
            "per_frame",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_FRAME,
            TC_SHADER_STAGE_VERTEX,
            termin::MaterialPipelineResourceOwner::VertexTransform),
        resource(
            "per_frame",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_FRAME,
            TC_SHADER_STAGE_FRAGMENT,
            termin::MaterialPipelineResourceOwner::Pass),
    }};

    termin::MaterialPipelineResourceMergeResult result =
        termin::material_pipeline_merge_resources(resources);

    REQUIRE(result.ok());
    REQUIRE_EQ(result.resources.size(), 1u);
    CHECK_EQ(result.resources[0].requirement.name, std::string("per_frame"));
    CHECK((result.resources[0].requirement.stage_mask & TC_SHADER_STAGE_VERTEX) != 0);
    CHECK((result.resources[0].requirement.stage_mask & TC_SHADER_STAGE_FRAGMENT) != 0);
}

TEST_CASE("Material pipeline resource merge permits distinct draw resources") {
    std::array<termin::MaterialPipelineResourceDecl, 2> resources{{
        resource(
            "draw_data",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_DRAW,
            TC_SHADER_STAGE_VERTEX,
            termin::MaterialPipelineResourceOwner::Material),
        resource(
            "foliage_draw",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_DRAW,
            TC_SHADER_STAGE_VERTEX,
            termin::MaterialPipelineResourceOwner::VertexTransform),
    }};

    termin::MaterialPipelineResourceMergeResult result =
        termin::material_pipeline_merge_resources(resources);

    REQUIRE(result.ok());
    REQUIRE_EQ(result.resources.size(), 2u);
}

TEST_CASE("Material pipeline resource merge rejects same-name contract conflicts") {
    std::array<termin::MaterialPipelineResourceDecl, 2> resources{{
        resource(
            "custom_params",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_MATERIAL,
            TC_SHADER_STAGE_FRAGMENT,
            termin::MaterialPipelineResourceOwner::Material),
        resource(
            "custom_params",
            TC_SHADER_RESOURCE_TEXTURE,
            TC_SHADER_RESOURCE_SCOPE_MATERIAL,
            TC_SHADER_STAGE_FRAGMENT,
            termin::MaterialPipelineResourceOwner::Material),
    }};

    termin::MaterialPipelineResourceMergeResult result =
        termin::material_pipeline_merge_resources(resources);

    REQUIRE(!result.ok());
    REQUIRE_EQ(result.diagnostics.size(), 1u);
    CHECK(
        result.diagnostics[0].code ==
        termin::MaterialPipelineDiagnosticCode::ResourceNameConflict);
    CHECK(result.diagnostics[0].message.find("custom_params") != std::string::npos);
}

TEST_CASE("Material pipeline resource merge combines same-name declarations by requirement") {
    std::array<termin::MaterialPipelineResourceDecl, 2> resources{{
        resource(
            "material",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_MATERIAL,
            TC_SHADER_STAGE_VERTEX,
            termin::MaterialPipelineResourceOwner::Material),
        resource(
            "material",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_MATERIAL,
            TC_SHADER_STAGE_FRAGMENT,
            termin::MaterialPipelineResourceOwner::Pass),
    }};

    termin::MaterialPipelineResourceMergeResult result =
        termin::material_pipeline_merge_resources(resources);

    REQUIRE(result.ok());
    REQUIRE_EQ(result.resources.size(), 1u);
    CHECK((result.resources[0].requirement.stage_mask & TC_SHADER_STAGE_VERTEX) != 0);
    CHECK((result.resources[0].requirement.stage_mask & TC_SHADER_STAGE_FRAGMENT) != 0);
}

TEST_CASE("Material pipeline resource merge canonicalizes shader ABI aliases") {
    std::array<termin::MaterialPipelineResourceDecl, 2> resources{{
        resource(
            TC_SHADER_RESOURCE_DRAW,
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_DRAW,
            TC_SHADER_STAGE_VERTEX,
            termin::MaterialPipelineResourceOwner::LegacyGlsl),
        resource(
            TC_SHADER_RESOURCE_DRAW_DATA,
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_DRAW,
            TC_SHADER_STAGE_FRAGMENT,
            termin::MaterialPipelineResourceOwner::VertexTransform),
    }};

    termin::MaterialPipelineResourceMergeResult result =
        termin::material_pipeline_merge_resources(resources);

    REQUIRE(result.ok());
    REQUIRE_EQ(result.resources.size(), 1u);
    CHECK_EQ(result.resources[0].requirement.name, std::string(TC_SHADER_RESOURCE_DRAW_DATA));
    CHECK((result.resources[0].requirement.stage_mask & TC_SHADER_STAGE_VERTEX) != 0);
    CHECK((result.resources[0].requirement.stage_mask & TC_SHADER_STAGE_FRAGMENT) != 0);
}

TEST_CASE("Material pipeline resource merge rejects shader ABI contract mismatch") {
    std::array<termin::MaterialPipelineResourceDecl, 1> resources{{
        resource(
            TC_SHADER_RESOURCE_MATERIAL,
            TC_SHADER_RESOURCE_TEXTURE,
            TC_SHADER_RESOURCE_SCOPE_MATERIAL,
            TC_SHADER_STAGE_FRAGMENT,
            termin::MaterialPipelineResourceOwner::Material),
    }};

    termin::MaterialPipelineResourceMergeResult result =
        termin::material_pipeline_merge_resources(resources);

    REQUIRE(!result.ok());
    REQUIRE_EQ(result.diagnostics.size(), 1u);
    CHECK(
        result.diagnostics[0].code ==
        termin::MaterialPipelineDiagnosticCode::AbiResourceContractMismatch);
    CHECK(result.diagnostics[0].message.find("material") != std::string::npos);
}

TEST_CASE("Material pipeline resource merge carries resource requirements") {
    std::vector<termin::MaterialPipelineResourceDecl> resources;
    termin::MaterialPipelineResourceDecl material{};
    material.requirement.name = "u_albedo_texture";
    material.requirement.kind = TC_SHADER_RESOURCE_TEXTURE;
    material.requirement.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    material.requirement.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    material.owner = termin::MaterialPipelineResourceOwner::Material;

    termin::MaterialPipelineResourceDecl incoming = material;
    incoming.requirement.stage_mask = TC_SHADER_STAGE_VERTEX;

    std::vector<termin::MaterialPipelineDiagnostic> diagnostics;
    CHECK(termin::material_pipeline_merge_resource(resources, material, diagnostics));
    CHECK(termin::material_pipeline_merge_resource(resources, incoming, diagnostics));

    REQUIRE(diagnostics.empty());
    REQUIRE_EQ(resources.size(), 1u);
    CHECK((resources[0].requirement.stage_mask & TC_SHADER_STAGE_VERTEX) != 0);
    CHECK((resources[0].requirement.stage_mask & TC_SHADER_STAGE_FRAGMENT) != 0);
}
