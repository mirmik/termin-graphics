#pragma once

#include <string>
#include <vector>

#include <termin/render/render_export.hpp>
#include <tgfx2/handles.hpp>

namespace termin {

    // One symbolic material source resolved for a concrete scene/render-target
    // context. Materials keep stable names; adapters provide scene-local handles.
    struct ResolvedMaterialTextureSource {
        std::string kind;
        std::string source_name;
        std::string channel;
        tgfx::TextureHandle texture;
    };

    using ResolvedMaterialTextureSources = std::vector<ResolvedMaterialTextureSource>;

} // namespace termin
