// material_ubo_apply.cpp - Apply material phase resources by reflected names.
#include "termin/render/material_ubo_apply.hpp"
#include "termin/render/shader_abi.hpp"
#include "termin/render/tgfx2_bridge.hpp"

#include "tgfx2/i_render_device.hpp"
#include "tgfx2/render_context.hpp"
#include "tcbase/tc_log.hpp"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

extern "C" {
#include "tgfx/resources/tc_shader_registry.h"
}

namespace termin {

namespace {

const char* shader_debug_name(const tc_shader* shader) {
    if (!shader) return "<null>";
    if (shader->name && shader->name[0] != '\0') return shader->name;
    if (shader->uuid[0] != '\0') return shader->uuid;
    return "<unnamed>";
}

void log_missing_material_resource_once(
    const tc_shader* shader,
    const char* resource_name,
    const char* reason)
{
    static std::mutex mutex;
    static std::unordered_set<std::string> emitted;

    std::string key = std::string(shader_debug_name(shader))
        + "\n"
        + (resource_name ? resource_name : "")
        + "\n"
        + (reason ? reason : "");
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!emitted.insert(key).second) {
            return;
        }
    }

    tc::Log::error(
        "[MaterialPipeline] shader '%s' is missing reflected material resource '%s': %s",
        shader_debug_name(shader),
        resource_name ? resource_name : "<null>",
        reason ? reason : "required by material phase");
}

inline bool type_is(const char* type, const char* expected) {
    return type && std::strcmp(type, expected) == 0;
}

inline void write_float(uint8_t* dst, float value) {
    std::memcpy(dst, &value, sizeof(value));
}

inline void write_int(uint8_t* dst, int32_t value) {
    std::memcpy(dst, &value, sizeof(value));
}

inline void write_float_array(uint8_t* dst, const float* values, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        write_float(dst + i * sizeof(float), values[i]);
    }
}

uint32_t std140_payload_size_for_type(const char* type) {
    if (type_is(type, "Float") || type_is(type, "Int") || type_is(type, "Bool")) {
        return 4u;
    }
    if (type_is(type, "Vec2")) {
        return 8u;
    }
    if (type_is(type, "Vec3")) {
        return 12u;
    }
    if (type_is(type, "Vec4") || type_is(type, "Color")) {
        return 16u;
    }
    if (type_is(type, "Mat4")) {
        return 64u;
    }
    return 0u;
}

const tc_uniform_value* find_phase_uniform(
    const tc_material_phase* phase,
    const char* name)
{
    if (!phase || !name || name[0] == '\0') {
        return nullptr;
    }
    for (size_t i = 0; i < phase->uniform_count; ++i) {
        if (std::strcmp(phase->uniforms[i].name, name) == 0) {
            return &phase->uniforms[i];
        }
    }
    return nullptr;
}

bool field_range_is_valid(
    const tc_shader* shader,
    const char* field_name,
    const char* field_type,
    uint32_t field_offset,
    uint32_t block_size)
{
    const uint32_t payload_size = std140_payload_size_for_type(field_type);
    if (payload_size == 0u) {
        return true;
    }
    if (field_offset <= block_size && payload_size <= block_size - field_offset) {
        return true;
    }

    tc::Log::error(
        "Material UBO field '%s' in shader '%s' exceeds block size "
        "(type=%s offset=%u payload=%u block=%u)",
        field_name ? field_name : "<unnamed>",
        shader_debug_name(shader),
        field_type ? field_type : "<null>",
        field_offset,
        payload_size,
        block_size);
    return false;
}

bool pack_uniform_value_to_std140_field(
    const tc_uniform_value& uniform,
    const char* field_type,
    uint8_t* dst)
{
    if (type_is(field_type, "Float")) {
        if (uniform.type == TC_UNIFORM_FLOAT) {
            write_float(dst, uniform.data.f);
            return true;
        }
        if (uniform.type == TC_UNIFORM_INT) {
            write_float(dst, static_cast<float>(uniform.data.i));
            return true;
        }
        return false;
    }
    if (type_is(field_type, "Int")) {
        if (uniform.type == TC_UNIFORM_INT) {
            write_int(dst, uniform.data.i);
            return true;
        }
        if (uniform.type == TC_UNIFORM_FLOAT) {
            write_int(dst, static_cast<int32_t>(uniform.data.f));
            return true;
        }
        return false;
    }
    if (type_is(field_type, "Bool")) {
        if (uniform.type == TC_UNIFORM_BOOL) {
            write_int(dst, uniform.data.i != 0 ? 1 : 0);
            return true;
        }
        return false;
    }
    if (type_is(field_type, "Vec2")) {
        if (uniform.type == TC_UNIFORM_VEC2) {
            write_float_array(dst, uniform.data.v2, 2);
            return true;
        }
        return false;
    }
    if (type_is(field_type, "Vec3")) {
        if (uniform.type == TC_UNIFORM_VEC3) {
            write_float_array(dst, uniform.data.v3, 3);
            return true;
        }
        return false;
    }
    if (type_is(field_type, "Vec4") || type_is(field_type, "Color")) {
        if (uniform.type == TC_UNIFORM_VEC4) {
            write_float_array(dst, uniform.data.v4, 4);
            return true;
        }
        return false;
    }
    if (type_is(field_type, "Mat4")) {
        if (uniform.type == TC_UNIFORM_MAT4) {
            write_float_array(dst, uniform.data.m4, 16);
            return true;
        }
        return false;
    }
    return false;
}

void pack_material_ubo_entry(
    const tc_material_phase* phase,
    const tc_shader* shader,
    const char* field_name,
    const char* field_type,
    uint32_t field_offset,
    uint32_t block_size,
    uint8_t* out_buffer)
{
    if (!field_range_is_valid(
            shader,
            field_name,
            field_type,
            field_offset,
            block_size)) {
        return;
    }
    const tc_uniform_value* uniform = find_phase_uniform(phase, field_name);
    if (!uniform) {
        return;
    }
    pack_uniform_value_to_std140_field(
        *uniform,
        field_type,
        out_buffer + field_offset);
}

bool pack_material_ubo_from_legacy_entries(
    const tc_material_phase* phase,
    const tc_shader* shader,
    uint8_t* out_buffer,
    uint32_t block_size)
{
    if (!shader || shader->material_ubo_entry_count == 0) {
        return false;
    }
    for (uint32_t i = 0; i < shader->material_ubo_entry_count; ++i) {
        const tc_material_ubo_entry& entry = shader->material_ubo_entries[i];
        pack_material_ubo_entry(
            phase,
            shader,
            entry.name,
            entry.property_type,
            entry.offset,
            block_size,
            out_buffer);
    }
    return true;
}

bool validate_reflected_material_fields(
    const tc_shader* shader,
    const tc_shader_resource_binding* material_rb)
{
    if (!shader || !material_rb || material_rb->field_count == 0 || !material_rb->fields) {
        return false;
    }
    for (uint32_t i = 0; i < material_rb->field_count; ++i) {
        const tc_shader_resource_field& field = material_rb->fields[i];
        if (field.type[0] == '\0') {
            tc::Log::error(
                "Material UBO field '%s' in shader '%s' has no reflected type",
                field.name,
                shader_debug_name(shader));
            return false;
        }
    }
    return true;
}

bool pack_material_ubo_from_reflected_fields(
    const tc_material_phase* phase,
    const tc_shader* shader,
    const tc_shader_resource_binding* material_rb,
    uint8_t* out_buffer,
    uint32_t block_size)
{
    if (!validate_reflected_material_fields(shader, material_rb)) {
        return false;
    }
    for (uint32_t i = 0; i < material_rb->field_count; ++i) {
        const tc_shader_resource_field& field = material_rb->fields[i];
        pack_material_ubo_entry(
            phase,
            shader,
            field.name,
            field.type,
            field.offset,
            block_size,
            out_buffer);
    }
    return true;
}

} // namespace

bool apply_material_phase_ubo(
    tc_material_phase* phase,
    const tc_shader* shader,
    tgfx::IRenderDevice& device,
    tgfx::RenderContext2& ctx)
{
    if (!phase || !shader) return false;

    const tc_shader_resource_binding* material_rb =
        find_shader_abi_resource_binding(shader, ShaderAbiResourceId::MaterialParams);

    bool bound_any = false;
    const bool has_legacy_material_ubo_layout =
        shader->material_ubo_entry_count > 0 && shader->material_ubo_block_size > 0;
    const bool has_reflected_material_fields =
        material_rb &&
        material_rb->kind == TC_SHADER_RESOURCE_CONSTANT_BUFFER &&
        material_rb->field_count > 0 &&
        material_rb->fields &&
        material_rb->size > 0;

    if (has_legacy_material_ubo_layout || has_reflected_material_fields) {
        const uint32_t block_size = has_legacy_material_ubo_layout
            ? shader->material_ubo_block_size
            : material_rb->size;
        std::vector<uint8_t> staging(block_size, 0);
        const bool packed_layout = has_legacy_material_ubo_layout
            ? pack_material_ubo_from_legacy_entries(
                phase,
                shader,
                staging.data(),
                block_size)
            : pack_material_ubo_from_reflected_fields(
                phase,
                shader,
                material_rb,
                staging.data(),
                block_size);

        if (!packed_layout) {
            // Invalid reflected metadata was logged above. Leave the old
            // behavior for "no usable layout": do not bind a material UBO.
        } else if (material_rb && material_rb->kind == TC_SHADER_RESOURCE_CONSTANT_BUFFER) {
            ctx.bind_uniform_data(
                TC_SHADER_RESOURCE_MATERIAL,
                staging.data(),
                static_cast<uint32_t>(staging.size()));
            bound_any = true;
        } else {
            log_missing_material_resource_once(
                shader,
                TC_SHADER_RESOURCE_MATERIAL,
                "material UBO layout exists but shader has no reflected material "
                "constant-buffer resource");
        }
    } else if (phase->uniform_count > 0 &&
               material_rb) {
        const char* reason = "reflected material constant buffer has no field metadata";
        if (material_rb && material_rb->kind != TC_SHADER_RESOURCE_CONSTANT_BUFFER) {
            reason = "reflected resource named 'material' is not a constant buffer";
        }
        log_missing_material_resource_once(
            shader,
            TC_SHADER_RESOURCE_MATERIAL,
            reason);
    }

    // Wrap each phase texture as a tgfx::TextureHandle. Textures bind by
    // material property name so RenderContext2 can resolve kind/scope/backend
    // placement from tc_shader_resource_binding.
    for (size_t i = 0; i < phase->texture_count; i++) {
        const tc_material_texture& mat_tex = phase->textures[i];
        if (tc_texture_handle_is_invalid(mat_tex.texture)) {
            continue;
        }
        tgfx::TextureHandle tex2 =
            wrap_tc_texture_as_tgfx2(device, mat_tex.texture);
        if (tex2) {
            const tc_shader_resource_binding* rb =
                tc_shader_find_resource_binding(shader, mat_tex.name);
            if (rb && rb->kind == TC_SHADER_RESOURCE_TEXTURE) {
                ctx.bind_texture(mat_tex.name, tex2);
                bound_any = true;
            } else {
                if (!rb && tc_shader_has_resource_layout(shader)) {
                    continue;
                }
                log_missing_material_resource_once(
                    shader,
                    mat_tex.name,
                    rb ? "reflected material resource is not a texture"
                       : "material texture has no reflected texture resource");
            }
        }
    }
    return bound_any;
}

} // namespace termin
