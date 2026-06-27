#pragma once

#include "termin/render/render_export.hpp"

extern "C" {
#include "tgfx/resources/tc_shader.h"
#include "tgfx/resources/tc_shader_registry.h"
}

namespace termin {

struct ShaderContractValidationOptions {
    const char* debug_context = nullptr;
    bool require_contract = false;
    bool validate_resource_layout = true;
};

// Validate a shader's generic interface contract against its resolved resource
// layout. Absence of a contract is allowed unless `require_contract` is set.
RENDER_API bool validate_shader_contract(
    const tc_shader* shader,
    const ShaderContractValidationOptions& options = {});

RENDER_API bool validate_shader_contract(
    tc_shader_handle shader_handle,
    const ShaderContractValidationOptions& options = {});

} // namespace termin
