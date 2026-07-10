#pragma once

#include <termin/render/render_export.hpp>

namespace termin {

// Registers pass factories and metadata owned by the core render library.
// The call is idempotent within one initialized runtime generation.
RENDER_API void register_builtin_render_pass_types();

} // namespace termin
