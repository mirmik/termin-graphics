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
    uint32_t binding,
    uint32_t stage_mask,
    termin::MaterialPipelineResourceOwner owner)
{
    termin::MaterialPipelineResourceDecl result{};
    result.name = name;
    result.kind = kind;
    result.scope = scope;
    result.set = TC_SHADER_RESOURCE_SET_DEFAULT;
    result.binding = binding;
    result.has_placement = true;
    result.stage_mask = stage_mask;
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
            2,
            TC_SHADER_STAGE_VERTEX,
            termin::MaterialPipelineResourceOwner::VertexTransform),
        resource(
            "per_frame",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_FRAME,
            2,
            TC_SHADER_STAGE_FRAGMENT,
            termin::MaterialPipelineResourceOwner::Pass),
    }};

    termin::MaterialPipelineResourceMergeResult result =
        termin::material_pipeline_merge_resources(resources);

    REQUIRE(result.ok());
    REQUIRE_EQ(result.resources.size(), 1u);
    CHECK_EQ(result.resources[0].name, std::string("per_frame"));
    CHECK((result.resources[0].stage_mask & TC_SHADER_STAGE_VERTEX) != 0);
    CHECK((result.resources[0].stage_mask & TC_SHADER_STAGE_FRAGMENT) != 0);
}

TEST_CASE("Material pipeline resource merge rejects placement conflicts") {
    std::array<termin::MaterialPipelineResourceDecl, 2> resources{{
        resource(
            "draw_data",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_DRAW,
            24,
            TC_SHADER_STAGE_VERTEX,
            termin::MaterialPipelineResourceOwner::Material),
        resource(
            "foliage_draw",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_DRAW,
            24,
            TC_SHADER_STAGE_VERTEX,
            termin::MaterialPipelineResourceOwner::VertexTransform),
    }};

    termin::MaterialPipelineResourceMergeResult result =
        termin::material_pipeline_merge_resources(resources);

    REQUIRE(!result.ok());
    REQUIRE_EQ(result.diagnostics.size(), 1u);
    CHECK(
        result.diagnostics[0].code ==
        termin::MaterialPipelineDiagnosticCode::ResourcePlacementConflict);
    CHECK(result.diagnostics[0].message.find("draw_data") != std::string::npos);
    CHECK(result.diagnostics[0].message.find("foliage_draw") != std::string::npos);
}

TEST_CASE("Material pipeline resource merge rejects same-name contract conflicts") {
    std::array<termin::MaterialPipelineResourceDecl, 2> resources{{
        resource(
            "material",
            TC_SHADER_RESOURCE_CONSTANT_BUFFER,
            TC_SHADER_RESOURCE_SCOPE_MATERIAL,
            1,
            TC_SHADER_STAGE_FRAGMENT,
            termin::MaterialPipelineResourceOwner::Material),
        resource(
            "material",
            TC_SHADER_RESOURCE_TEXTURE,
            TC_SHADER_RESOURCE_SCOPE_MATERIAL,
            1,
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
    CHECK(result.diagnostics[0].message.find("material") != std::string::npos);
}

TEST_CASE("Material pipeline resource merge carries unplaced declarations") {
    std::vector<termin::MaterialPipelineResourceDecl> resources;
    termin::MaterialPipelineResourceDecl material{};
    material.name = "u_albedo_texture";
    material.kind = TC_SHADER_RESOURCE_TEXTURE;
    material.scope = TC_SHADER_RESOURCE_SCOPE_MATERIAL;
    material.has_placement = false;
    material.stage_mask = TC_SHADER_STAGE_FRAGMENT;
    material.owner = termin::MaterialPipelineResourceOwner::Material;

    termin::MaterialPipelineResourceDecl incoming = material;
    incoming.stage_mask = TC_SHADER_STAGE_VERTEX;

    std::vector<termin::MaterialPipelineDiagnostic> diagnostics;
    CHECK(termin::material_pipeline_merge_resource(resources, material, diagnostics));
    CHECK(termin::material_pipeline_merge_resource(resources, incoming, diagnostics));

    REQUIRE(diagnostics.empty());
    REQUIRE_EQ(resources.size(), 1u);
    CHECK(!resources[0].has_placement);
    CHECK((resources[0].stage_mask & TC_SHADER_STAGE_VERTEX) != 0);
    CHECK((resources[0].stage_mask & TC_SHADER_STAGE_FRAGMENT) != 0);
}
