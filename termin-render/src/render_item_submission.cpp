#include <termin/render/render_item_submission.hpp>

#include "render_item_mesh.hpp"

#include <tcbase/tc_log.hpp>

#include <mutex>
#include <string>
#include <unordered_map>

namespace termin {
namespace {

struct RegisteredRenderItemDrawEncoder {
    RenderItemDrawEncoderFn encode = nullptr;
    void* user_data = nullptr;
    std::string debug_name;
};

std::mutex& render_item_draw_encoder_mutex()
{
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<uint32_t, RegisteredRenderItemDrawEncoder>&
render_item_draw_encoder_registry()
{
    static std::unordered_map<uint32_t, RegisteredRenderItemDrawEncoder> registry;
    return registry;
}

bool find_registered_render_item_draw_encoder(
    uint32_t item_kind,
    RegisteredRenderItemDrawEncoder& out_encoder)
{
    std::lock_guard<std::mutex> lock(render_item_draw_encoder_mutex());
    auto& registry = render_item_draw_encoder_registry();
    auto it = registry.find(item_kind);
    if (it == registry.end()) {
        return false;
    }
    out_encoder = it->second;
    return true;
}

} // namespace

bool register_render_item_draw_encoder(
    uint32_t item_kind,
    const RenderItemDrawEncoderDesc& desc)
{
    if (item_kind == TC_RENDER_ITEM_KIND_INVALID) {
        tc::Log::error(
            "[RenderItemSubmit] cannot register draw encoder for invalid item kind");
        return false;
    }
    if (!desc.encode) {
        tc::Log::error(
            "[RenderItemSubmit] cannot register null draw encoder for item kind %u",
            item_kind);
        return false;
    }

    std::lock_guard<std::mutex> lock(render_item_draw_encoder_mutex());
    auto& registry = render_item_draw_encoder_registry();
    if (registry.find(item_kind) != registry.end()) {
        tc::Log::error(
            "[RenderItemSubmit] draw encoder for item kind %u is already registered",
            item_kind);
        return false;
    }

    RegisteredRenderItemDrawEncoder registered{};
    registered.encode = desc.encode;
    registered.user_data = desc.user_data;
    registered.debug_name = desc.debug_name ? desc.debug_name : "";
    registry.emplace(item_kind, std::move(registered));
    return true;
}

bool unregister_render_item_draw_encoder(
    uint32_t item_kind,
    RenderItemDrawEncoderFn encode,
    void* user_data)
{
    std::lock_guard<std::mutex> lock(render_item_draw_encoder_mutex());
    auto& registry = render_item_draw_encoder_registry();
    auto it = registry.find(item_kind);
    if (it == registry.end()) {
        tc::Log::error(
            "[RenderItemSubmit] cannot unregister draw encoder for item kind %u: no encoder registered",
            item_kind);
        return false;
    }
    if (it->second.encode != encode || it->second.user_data != user_data) {
        tc::Log::error(
            "[RenderItemSubmit] cannot unregister draw encoder for item kind %u: callback mismatch",
            item_kind);
        return false;
    }
    registry.erase(it);
    return true;
}

bool submit_render_item_draw(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const RenderItemDrawSubmitRequest& request)
{
    const char* pass_name = request.debug_pass_name
        ? request.debug_pass_name
        : "RenderItemSubmit";
    const char* entity_name = request.debug_entity_name
        ? request.debug_entity_name
        : "<unnamed>";

    switch (item.kind) {
        case TC_RENDER_ITEM_KIND_MESH: {
            MeshRenderItemEncodeRequest mesh_request{};
            mesh_request.shader = request.shader;
            mesh_request.vertex_input = request.mesh_vertex_input;
            mesh_request.debug_pass_name = pass_name;
            mesh_request.debug_entity_name = entity_name;
            return encode_mesh_render_item_draw(ctx, item, mesh_request);
        }
        default:
            RegisteredRenderItemDrawEncoder encoder{};
            if (find_registered_render_item_draw_encoder(item.kind, encoder)) {
                return encoder.encode(ctx, item, request, encoder.user_data);
            }
            tc::Log::error(
                "[%s] skip RenderItem draw for '%s': unsupported item kind %u",
                pass_name,
                entity_name,
                item.kind);
            return false;
    }
}

} // namespace termin
