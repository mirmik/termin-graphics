#include "termin/render/shader_contract.hpp"

#include "tcbase/tc_log.hpp"

#include <cstring>

namespace termin {
namespace {

const char* context_name(const ShaderContractValidationOptions& options)
{
    return options.debug_context ? options.debug_context : "ShaderContract";
}

const char* shader_name(const tc_shader* shader)
{
    if (!shader) {
        return "<null>";
    }
    if (shader->name && shader->name[0] != '\0') {
        return shader->name;
    }
    if (shader->uuid[0] != '\0') {
        return shader->uuid;
    }
    return "<unnamed>";
}

bool stage_mask_covers(uint32_t layout_stage_mask, uint32_t required_stage_mask)
{
    if (required_stage_mask == 0) {
        return true;
    }
    return (layout_stage_mask & required_stage_mask) == required_stage_mask;
}

bool validate_resource_requirement(
    const tc_shader* shader,
    const tc_shader_resource_requirement& requirement,
    const ShaderContractValidationOptions& options)
{
    const tc_shader_resource_binding* binding =
        tc_shader_find_resource_binding(shader, requirement.name);
    if (!binding) {
        tc::Log::error(
            "[%s] shader '%s' contract requires resource '%s', but resource layout has no matching entry",
            context_name(options),
            shader_name(shader),
            requirement.name);
        return false;
    }

    if (binding->kind != requirement.kind) {
        tc::Log::error(
            "[%s] shader '%s' resource '%s' kind mismatch: contract=%u layout=%u",
            context_name(options),
            shader_name(shader),
            requirement.name,
            requirement.kind,
            binding->kind);
        return false;
    }

    if (binding->scope != requirement.scope) {
        tc::Log::error(
            "[%s] shader '%s' resource '%s' scope mismatch: contract=%u layout=%u",
            context_name(options),
            shader_name(shader),
            requirement.name,
            requirement.scope,
            binding->scope);
        return false;
    }

    if (!stage_mask_covers(binding->stage_mask, requirement.stage_mask)) {
        tc::Log::error(
            "[%s] shader '%s' resource '%s' stage mismatch: contract=0x%x layout=0x%x",
            context_name(options),
            shader_name(shader),
            requirement.name,
            requirement.stage_mask,
            binding->stage_mask);
        return false;
    }

    if (requirement.size > 0 && binding->size > 0 && binding->size < requirement.size) {
        tc::Log::error(
            "[%s] shader '%s' resource '%s' layout size %u is smaller than contract size %u",
            context_name(options),
            shader_name(shader),
            requirement.name,
            binding->size,
            requirement.size);
        return false;
    }

    return true;
}

} // namespace

bool validate_shader_contract(
    const tc_shader* shader,
    const ShaderContractValidationOptions& options)
{
    if (!shader) {
        tc::Log::error(
            "[%s] cannot validate shader contract: shader is null",
            context_name(options));
        return false;
    }

    tc_shader_contract_view contract{};
    if (!tc_shader_get_contract_view(shader, &contract)) {
        if (options.require_contract) {
            tc::Log::error(
                "[%s] shader '%s' is on a migrated render path but has no tc_shader_contract",
                context_name(options),
                shader_name(shader));
            return false;
        }
        return true;
    }

    if (!options.validate_resource_layout || contract.resource_count == 0) {
        return true;
    }

    if (!tc_shader_has_resource_layout(shader)) {
        tc::Log::error(
            "[%s] shader '%s' has tc_shader_contract resources but no known resource layout",
            context_name(options),
            shader_name(shader));
        return false;
    }

    for (uint32_t i = 0; i < contract.resource_count; ++i) {
        if (!validate_resource_requirement(shader, contract.resources[i], options)) {
            return false;
        }
    }

    return true;
}

bool validate_shader_contract(
    tc_shader_handle shader_handle,
    const ShaderContractValidationOptions& options)
{
    tc_shader* shader = tc_shader_get(shader_handle);
    if (!shader) {
        tc::Log::error(
            "[%s] shader handle is stale (index=%u gen=%u)",
            context_name(options),
            shader_handle.index,
            shader_handle.generation);
        return false;
    }
    return validate_shader_contract(shader, options);
}

} // namespace termin
