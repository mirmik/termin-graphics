#pragma once

#include <termin/geom/mat44.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/tgfx2_api.h>

namespace tgfx {

TGFX2_API termin::Mat44f adapt_projection_for_backend(
    BackendType backend,
    const termin::Mat44f& projection);

TGFX2_API termin::Mat44 adapt_projection_for_backend(
    BackendType backend,
    const termin::Mat44& projection);

} // namespace tgfx
