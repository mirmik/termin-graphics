#pragma once

#include <cstddef>

#include <core/tc_render_item.h>
#include <tgfx/tgfx_mesh_handle.hpp>

namespace termin {

struct PythonRenderItem {
    tc_render_item item{};

    PythonRenderItem() = default;

    static PythonRenderItem mesh(
        const TcMesh& mesh,
        tc_material_phase* phase,
        int geometry_id = 0,
        size_t submesh_index = 0);
};

} // namespace termin
