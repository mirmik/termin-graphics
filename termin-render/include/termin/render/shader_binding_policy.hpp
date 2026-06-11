// shader_binding_policy.hpp - shared rules for layout-only vs legacy binding.
#pragma once

extern "C" {
#include "tgfx/resources/tc_shader.h"
}

namespace termin {

inline bool shader_has_layout_metadata(const tc_shader* shader) {
    return tc_shader_has_resource_layout(shader);
}

inline bool shader_uses_layout_only_bindings(const tc_shader* shader) {
    return shader
        && tc_shader_has_resource_layout(shader)
        && tc_shader_get_language(shader) != TC_SHADER_LANGUAGE_GLSL;
}

inline bool shader_allows_legacy_resource_fallback(const tc_shader* shader) {
    return !shader_uses_layout_only_bindings(shader);
}

} // namespace termin
