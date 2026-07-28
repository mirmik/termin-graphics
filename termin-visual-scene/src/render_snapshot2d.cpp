#include "termin_visual_scene/render_snapshot2d.hpp"

#include <utility>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
namespace {

bool push_item(
    tgfx::DrawList2DBuilder& builder,
    const GraphicItemSnapshot2D& item,
    SceneRenderResourceResolver2D& resolver) {
    if (!item.effective_visible || item.effective_opacity <= 0.0f) return true;
    if (std::holds_alternative<GroupItem2D>(item.payload) ||
        std::holds_alternative<HitRegionItem2D>(item.payload)) {
        return true;
    }

    std::size_t clips = 0;
    for (const auto& clip : item.effective_clips) {
        if (!builder.push_clip(clip.path, clip.rule)) return false;
        ++clips;
    }
    if (!builder.push_opacity(item.effective_opacity) ||
        !builder.push_transform(item.world_transform)) {
        return false;
    }

    const bool drawn = std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, RectItem2D>) {
                if (value.stroke) {
                    return builder.rounded_rect(
                        value.rect, 0.0f, value.fill, value.stroke);
                }
                return builder.rect(value.rect, value.fill);
            } else if constexpr (std::is_same_v<T, RoundedRectItem2D>) {
                return builder.rounded_rect(
                    value.rect, value.radius, value.fill, value.stroke);
            } else if constexpr (std::is_same_v<T, EllipseItem2D>) {
                return builder.ellipse(value.bounds, value.fill, value.stroke);
            } else if constexpr (std::is_same_v<T, PathItem2D>) {
                return builder.path(value.path, value.fill, value.stroke);
            } else if constexpr (std::is_same_v<T, PolylineItem2D>) {
                return builder.polyline(
                    value.points, value.stroke, value.closed);
            } else if constexpr (std::is_same_v<T, TextItem2D>) {
                const auto font = resolver.resolve_font(value.font);
                if (!font || !*font) {
                    tc::Log::error(
                        "VisualScene2D render preparation: font '%s' was not resolved",
                        value.font.uri.c_str());
                    return false;
                }
                return builder.text(
                    value.text,
                    value.origin,
                    value.size_px,
                    value.color,
                    *font,
                    value.anchor);
            } else if constexpr (std::is_same_v<T, ImageItem2D>) {
                const auto texture = resolver.resolve_image(value.image);
                if (!texture || !*texture) {
                    tc::Log::error(
                        "VisualScene2D render preparation: image '%s' was not resolved",
                        value.image.uri.c_str());
                    return false;
                }
                return builder.image(
                    *texture,
                    value.rect,
                    value.uv,
                    value.tint,
                    value.sampling);
            } else if constexpr (std::is_same_v<T, CustomBatchItem2D>) {
                const auto batch = resolver.resolve_custom_batch(value);
                if (!batch) {
                    tc::Log::error(
                        "VisualScene2D render preparation: custom batch '%s' was not resolved",
                        value.key.c_str());
                    return false;
                }
                return builder.custom_batch(
                    batch->vertices,
                    batch->color,
                    batch->texture,
                    batch->sampling);
            } else {
                return true;
            }
        },
        item.payload);

    if (!drawn || !builder.pop_transform() || !builder.pop_opacity()) {
        return false;
    }
    while (clips > 0) {
        if (!builder.pop_clip()) return false;
        --clips;
    }
    return true;
}

}  // namespace

std::optional<SceneRenderSnapshot2D> VisualScene2D::prepare_render_snapshot(
    SceneRenderResourceResolver2D& resolver) const {
    std::vector<GraphicItemSnapshot2D> copied_items;
    std::uint64_t copied_revision = 0;
    {
        std::scoped_lock lock(mutex_);
        copied_items = snapshots_locked_();
        copied_revision = revision_;
    }

    tgfx::DrawList2DBuilder builder;
    for (const auto& item : copied_items) {
        if (!push_item(builder, item, resolver)) {
            tc::Log::error(
                "VisualScene2D render preparation failed at item %u:%u",
                item.handle.index,
                item.handle.generation);
            return std::nullopt;
        }
    }
    auto draw_list = builder.freeze();
    if (!draw_list) {
        tc::Log::error(
            "VisualScene2D render preparation produced an unbalanced DrawList2D");
        return std::nullopt;
    }
    return SceneRenderSnapshot2D(
        copied_revision,
        std::move(copied_items),
        std::move(*draw_list));
}

}  // namespace termin::visual
