#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include <termin/geom/bounds2.hpp>
#include <tgfx2/draw_list2d.hpp>

#include "termin_visual_scene/export.h"

namespace termin::visual {

    struct ResolvedCustomBatch2D {
        std::vector<tgfx::DrawVertex2D> vertices;
        termin::LinearColor color{1.0f, 1.0f, 1.0f, 1.0f};
        tgfx::TextureHandle texture{};
        tgfx::DrawTextureSampling2D sampling = tgfx::DrawTextureSampling2D::Linear;
    };

    // Resolution runs synchronously during thread-confined scene traversal.
    // Returned runtime handles are borrowed and must remain live while the
    // resulting DrawList2D is executed.
    class TERMIN_VISUAL_SCENE_API SceneRenderResourceResolver2D {
    public:
        virtual ~SceneRenderResourceResolver2D() = default;
        virtual std::optional<tgfx::FontHandle> resolve_font(std::string_view uri) = 0;
        virtual std::optional<tgfx::TextureHandle> resolve_image(std::string_view uri) = 0;
        virtual std::optional<ResolvedCustomBatch2D> resolve_custom_batch(std::string_view key,
                                                                          termin::Bounds2f local_bounds) = 0;
    };

} // namespace termin::visual
