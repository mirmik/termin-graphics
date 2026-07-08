#include "render_item_mesh.hpp"

#include <termin/render/shader_abi.hpp>

#include <algorithm>
#include <array>
#include <cstring>

#include <tcbase/tc_log.hpp>
#include <tgfx2/render_context.hpp>

extern "C" {
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/resources/tc_mesh_registry.h>
}

namespace termin {
namespace {

inline constexpr uint32_t RENDER_ITEM_BONE_BLOCK_MAX_BONES = 128;
inline constexpr uint64_t RENDER_ITEM_BONE_BLOCK_SIZE =
    RENDER_ITEM_BONE_BLOCK_MAX_BONES * 16u * sizeof(float) + 16u;

struct RenderItemMeshDrawGeometry {
    tc_mesh* mesh = nullptr;
    size_t submesh_index = 0;
};

bool resolve_render_item_mesh_draw_geometry(
    const tc_render_item& item,
    const char* pass_name,
    const char* entity_name,
    RenderItemMeshDrawGeometry& out_geometry)
{
    out_geometry = {};
    const char* pass = pass_name ? pass_name : "RenderItemMesh";
    const char* name = entity_name ? entity_name : "<unnamed>";

    if (item.kind != TC_RENDER_ITEM_KIND_MESH) {
        tc::Log::error(
            "[%s] skip RenderItem draw for '%s': item kind %u is not mesh",
            pass,
            name,
            item.kind);
        return false;
    }

    const tc_mesh_handle mesh_handle = item.payload.mesh.mesh_handle;
    if (tc_mesh_handle_is_invalid(mesh_handle)) {
        tc::Log::error(
            "[%s] skip RenderItem mesh draw for '%s': mesh payload has no stable handle",
            pass,
            name);
        return false;
    }

    tc_mesh* mesh = tc_mesh_get(mesh_handle);
    if (!mesh) {
        tc::Log::error(
            "[%s] skip RenderItem mesh draw for '%s': mesh handle is stale or invalid",
            pass,
            name);
        return false;
    }
    if (mesh->submesh_count == 0) {
        tc::Log::error(
            "[%s] skip RenderItem mesh draw for '%s': mesh has no submeshes",
            pass,
            name);
        return false;
    }

    const tc_submesh* submesh = tc_mesh_get_submesh(mesh, item.payload.mesh.submesh_index);
    if (!submesh) {
        tc::Log::error(
            "[%s] skip RenderItem mesh draw for '%s': mesh has no submesh %zu (count=%zu)",
            pass,
            name,
            item.payload.mesh.submesh_index,
            mesh->submesh_count);
        return false;
    }
    if (submesh->index_count == 0 ||
        static_cast<size_t>(submesh->first_index) > mesh->index_count ||
        static_cast<size_t>(submesh->index_count) >
            mesh->index_count - static_cast<size_t>(submesh->first_index)) {
        tc::Log::error(
            "[%s] skip RenderItem mesh draw for '%s': invalid submesh %zu first=%u count=%u mesh_indices=%zu",
            pass,
            name,
            item.payload.mesh.submesh_index,
            submesh->first_index,
            submesh->index_count,
            mesh->index_count);
        return false;
    }

    out_geometry.mesh = mesh;
    out_geometry.submesh_index = item.payload.mesh.submesh_index;
    return true;
}

using RenderItemBoneBlockStorage =
    std::array<uint8_t, static_cast<size_t>(RENDER_ITEM_BONE_BLOCK_SIZE)>;

void pack_render_item_bone_block_std140(
    const tc_render_item& item,
    RenderItemBoneBlockStorage& staging)
{
    staging.fill(0);
    const uint32_t matrix_count = std::min<uint32_t>(
        item.payload.mesh.skinning_matrix_count,
        RENDER_ITEM_BONE_BLOCK_MAX_BONES);
    if (matrix_count > 0 && item.payload.mesh.skinning_matrices) {
        std::memcpy(
            staging.data(),
            item.payload.mesh.skinning_matrices,
            matrix_count * 16u * sizeof(float));
    }
    int32_t count = static_cast<int32_t>(matrix_count);
    std::memcpy(
        staging.data() + RENDER_ITEM_BONE_BLOCK_MAX_BONES * 16u * sizeof(float),
        &count,
        sizeof(int32_t));
}

bool render_item_requires_bone_block(const tc_render_item& item)
{
    return (item.flags & TC_RENDER_ITEM_FLAG_HAS_SKINNING_MATRICES) != 0u;
}

bool shader_accepts_render_item_bone_block(
    const tc_shader* shader,
    const char* pass_name,
    const char* entity_name)
{
    if (find_shader_abi_resource_binding(shader, ShaderAbiResourceId::BoneBlock)) {
        return true;
    }
    tc::Log::error(
        "[%s] skip skinned RenderItem mesh draw for '%s': shader '%s' has no %s resource",
        pass_name ? pass_name : "RenderItemMesh",
        entity_name ? entity_name : "<unnamed>",
        shader && shader->name ? shader->name : "<unnamed>",
        TC_SHADER_RESOURCE_BONE_BLOCK);
    return false;
}

} // namespace

bool encode_mesh_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const MeshRenderItemEncodeRequest& request)
{
    const char* pass_name = request.debug_pass_name
        ? request.debug_pass_name
        : "RenderItemMesh";
    const char* entity_name = request.debug_entity_name
        ? request.debug_entity_name
        : "<unnamed>";

    RenderItemMeshDrawGeometry mesh_geometry{};
    if (!resolve_render_item_mesh_draw_geometry(
            item,
            pass_name,
            entity_name,
            mesh_geometry)) {
        return false;
    }

    if (!request.shader) {
        tc::Log::error(
            "[%s] skip RenderItem mesh draw for '%s': request has no shader",
            pass_name,
            entity_name);
        return false;
    }

    if (render_item_requires_bone_block(item)) {
        if (!shader_accepts_render_item_bone_block(
                request.shader,
                pass_name,
                entity_name)) {
            return false;
        }
        RenderItemBoneBlockStorage bone_block{};
        pack_render_item_bone_block_std140(item, bone_block);
        ctx.bind_uniform_data(
            TC_SHADER_RESOURCE_BONE_BLOCK,
            bone_block.data(),
            static_cast<uint32_t>(bone_block.size()));
    }

    draw_material_pipeline_submesh(
        ctx,
        mesh_geometry.mesh,
        mesh_geometry.submesh_index,
        material_mesh_vertex_input_for_shader(request.shader, request.vertex_input));
    return true;
}

} // namespace termin
