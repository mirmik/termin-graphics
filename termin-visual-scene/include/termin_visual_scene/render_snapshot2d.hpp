#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <tgfx2/draw_list2d.hpp>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/scene2d.hpp"

namespace termin::visual {

struct ResolvedCustomBatch2D {
    std::vector<tgfx::DrawVertex2D> vertices;
    tgfx::Color4f color{};
    tgfx::TextureHandle texture{};
    tgfx::DrawTextureSampling2D sampling =
        tgfx::DrawTextureSampling2D::Linear;
};

// Resolution is deliberately host-supplied and runs after the scene lock has
// been released. Returned runtime handles are borrowed: the host must keep
// them live through Canvas2DRenderer::execute(snapshot.draw_list(), ...).
class TERMIN_VISUAL_SCENE_API SceneRenderResourceResolver2D {
public:
    virtual ~SceneRenderResourceResolver2D() = default;
    virtual std::optional<tgfx::FontHandle> resolve_font(
        const StableResourceRef2D& reference) = 0;
    virtual std::optional<tgfx::TextureHandle> resolve_image(
        const StableResourceRef2D& reference) = 0;
    virtual std::optional<ResolvedCustomBatch2D> resolve_custom_batch(
        const CustomBatchItem2D& reference) = 0;
};

class TERMIN_VISUAL_SCENE_API SceneRenderSnapshot2D {
public:
    std::uint64_t revision() const noexcept { return revision_; }
    std::span<const GraphicItemSnapshot2D> items() const noexcept {
        return items_;
    }
    const tgfx::DrawList2D& draw_list() const noexcept { return draw_list_; }

private:
    SceneRenderSnapshot2D(
        std::uint64_t revision,
        std::vector<GraphicItemSnapshot2D> items,
        tgfx::DrawList2D draw_list)
        : revision_(revision),
          items_(std::move(items)),
          draw_list_(std::move(draw_list)) {}

    std::uint64_t revision_ = 0;
    std::vector<GraphicItemSnapshot2D> items_;
    tgfx::DrawList2D draw_list_;
    friend class VisualScene2D;
};

}  // namespace termin::visual
