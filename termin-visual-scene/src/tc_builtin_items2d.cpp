#include "termin_visual_scene/tc_builtin_items2d.h"

#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <termin/geom/color.hpp>

#include "termin_visual_scene/builtin_items2d.hpp"

namespace {

    using termin::visual::GraphicItem2D;
    using termin::visual::TcVisualScene;

    std::optional<tgfx::FillRule> fill_rule(tc_visual_fill_rule2d value) {
        if (value == TC_VISUAL_FILL_RULE_NON_ZERO) {
            return tgfx::FillRule::NonZero;
        }
        if (value == TC_VISUAL_FILL_RULE_EVEN_ODD) {
            return tgfx::FillRule::EvenOdd;
        }
        return std::nullopt;
    }

    termin::LinearColor color(tc_visual_color4f value) {
        return {value.r, value.g, value.b, value.a};
    }

    termin::SrgbColor authored_color(tc_visual_color4f value) {
        return {value.r, value.g, value.b, value.a};
    }

    std::optional<tgfx::FillPaint> fill(const tc_visual_fill_paint2d* value) {
        if (value == nullptr)
            return std::nullopt;
        const auto rule = fill_rule(value->rule);
        if (!rule)
            return std::nullopt;
        return tgfx::FillPaint{color(value->color), *rule};
    }

    std::optional<tgfx::StrokePaint> stroke(const tc_visual_stroke_paint2d* value) {
        if (value == nullptr)
            return std::nullopt;
        tgfx::StrokeJoin join;
        switch (value->join) {
        case TC_VISUAL_STROKE_JOIN_MITER:
            join = tgfx::StrokeJoin::Miter;
            break;
        case TC_VISUAL_STROKE_JOIN_ROUND:
            join = tgfx::StrokeJoin::Round;
            break;
        case TC_VISUAL_STROKE_JOIN_BEVEL:
            join = tgfx::StrokeJoin::Bevel;
            break;
        default:
            return std::nullopt;
        }
        tgfx::StrokeCap cap;
        switch (value->cap) {
        case TC_VISUAL_STROKE_CAP_BUTT:
            cap = tgfx::StrokeCap::Butt;
            break;
        case TC_VISUAL_STROKE_CAP_ROUND:
            cap = tgfx::StrokeCap::Round;
            break;
        case TC_VISUAL_STROKE_CAP_SQUARE:
            cap = tgfx::StrokeCap::Square;
            break;
        default:
            return std::nullopt;
        }
        if (value->dash_count != 0 && value->dash_pattern == nullptr) {
            return std::nullopt;
        }
        std::vector<float> dash_pattern;
        if (value->dash_count != 0) {
            dash_pattern.assign(value->dash_pattern, value->dash_pattern + value->dash_count);
        }
        return tgfx::StrokePaint{
            .color = color(value->color),
            .width = value->width,
            .join = join,
            .cap = cap,
            .miter_limit = value->miter_limit,
            .dash_pattern = std::move(dash_pattern),
            .dash_offset = value->dash_offset,
        };
    }

    bool make_path(tc_visual_path2d_view source, tgfx::Path2f& out) {
        if ((source.verb_count != 0 && source.verbs == nullptr) ||
            (source.point_count != 0 && source.points == nullptr)) {
            return false;
        }
        std::vector<tgfx::Path2Verb> verbs;
        verbs.reserve(source.verb_count);
        for (std::size_t index = 0; index < source.verb_count; ++index) {
            switch (source.verbs[index]) {
            case TC_VISUAL_PATH_MOVE_TO:
                verbs.push_back(tgfx::Path2Verb::MoveTo);
                break;
            case TC_VISUAL_PATH_LINE_TO:
                verbs.push_back(tgfx::Path2Verb::LineTo);
                break;
            case TC_VISUAL_PATH_QUADRATIC_TO:
                verbs.push_back(tgfx::Path2Verb::QuadraticTo);
                break;
            case TC_VISUAL_PATH_CUBIC_TO:
                verbs.push_back(tgfx::Path2Verb::CubicTo);
                break;
            case TC_VISUAL_PATH_CLOSE:
                verbs.push_back(tgfx::Path2Verb::Close);
                break;
            default:
                return false;
            }
        }
        return out.try_assign(verbs, std::span<const termin::Vec2f>{source.points, source.point_count});
    }

    tc_graphic_item* parent_object(tc_visual_scene_handle scene, tc_graphic_item_handle parent) {
        if (tc_graphic_item_handle_is_invalid(parent))
            return nullptr;
        tc_graphic_item* result = tc_visual_scene_resolve_item(scene, parent);
        if (result == nullptr) {
            tc::Log::error("built-in item factory received a stale or foreign parent");
        }
        return result;
    }

    template <typename Item>
    tc_graphic_item_handle
    adopt(tc_visual_scene_handle scene, tc_graphic_item_handle parent, std::unique_ptr<Item> object) {
        if (!tc_visual_scene_is_valid(scene)) {
            tc::Log::error("built-in item factory received an invalid scene");
            return tc_graphic_item_handle_invalid();
        }
        tc_graphic_item* parent_item = parent_object(scene, parent);
        if (!tc_graphic_item_handle_is_invalid(parent) && parent_item == nullptr) {
            return tc_graphic_item_handle_invalid();
        }
        if (parent_item != nullptr &&
            (parent_item->native_language != TC_LANGUAGE_CXX || parent_item->body == nullptr)) {
            tc::Log::error("built-in item factory parent has no native C++ body");
            return tc_graphic_item_handle_invalid();
        }
        const auto adopted = TcVisualScene{scene}.adopt(
            std::move(object), parent_item != nullptr ? static_cast<GraphicItem2D*>(parent_item->body) : nullptr);
        return adopted.value_or(tc_graphic_item_handle_invalid());
    }

    template <typename Item, typename Factory>
    tc_graphic_item_handle
    create(tc_visual_scene_handle scene, tc_graphic_item_handle parent, const char* operation, Factory&& factory) {
        try {
            return adopt<Item>(scene, parent, factory());
        } catch (const std::exception& error) {
            tc::Log::error(operation, ": ", error.what());
            return tc_graphic_item_handle_invalid();
        }
    }

    template <typename Item, typename Factory>
    bool replace(tc_visual_scene_handle scene,
                 tc_graphic_item_handle item,
                 const char* expected_type,
                 const char* operation,
                 Factory&& factory) {
        if (!tc_visual_scene_item_is_type(scene, item, expected_type)) {
            tc::Log::error(operation, ": wrong, stale or cross-scene item");
            return false;
        }
        try {
            tc_graphic_item* previous = tc_visual_scene_resolve_item(scene, item);
            if (previous == nullptr || previous->body == nullptr || previous->native_language != TC_LANGUAGE_CXX) {
                tc::Log::error(operation, ": item has no native C++ body");
                return false;
            }
            const auto& previous_object = *static_cast<GraphicItem2D*>(previous->body);
            auto replacement = factory();
            replacement->set_clip(previous_object.clip());
            Item* raw = replacement.release();
            return tc_visual_scene_replace_item(scene, item, raw->c_item(), &GraphicItem2D::delete_owned_item);
        } catch (const std::exception& error) {
            tc::Log::error(operation, ": ", error.what());
            return false;
        }
    }

    std::optional<tgfx::TextAnchor2D> text_anchor(tc_visual_text_anchor2d value) {
        if (value == TC_VISUAL_TEXT_ANCHOR_LEFT) {
            return tgfx::TextAnchor2D::Left;
        }
        if (value == TC_VISUAL_TEXT_ANCHOR_CENTER) {
            return tgfx::TextAnchor2D::Center;
        }
        if (value == TC_VISUAL_TEXT_ANCHOR_RIGHT) {
            return tgfx::TextAnchor2D::Right;
        }
        return std::nullopt;
    }

    std::optional<tgfx::DrawTextureSampling2D> sampling(tc_visual_texture_sampling2d value) {
        if (value == TC_VISUAL_TEXTURE_SAMPLING_LINEAR) {
            return tgfx::DrawTextureSampling2D::Linear;
        }
        if (value == TC_VISUAL_TEXTURE_SAMPLING_NEAREST) {
            return tgfx::DrawTextureSampling2D::Nearest;
        }
        return std::nullopt;
    }

} // namespace

extern "C" {

tc_graphic_item_handle tc_visual_group_item2d_create(tc_visual_scene_handle scene, tc_graphic_item_handle parent) {
    return create<termin::visual::GroupItem2D>(
        scene, parent, "create GroupItem2D", [] { return std::make_unique<termin::visual::GroupItem2D>(); });
}

tc_graphic_item_handle tc_visual_rect_item2d_create(tc_visual_scene_handle scene,
                                                    tc_graphic_item_handle parent,
                                                    tc_rect2f rect,
                                                    tc_visual_fill_paint2d fill_value,
                                                    const tc_visual_stroke_paint2d* stroke_value) {
    return create<termin::visual::RectItem2D>(scene, parent, "create RectItem2D", [&] {
        const auto fill_paint = fill(&fill_value);
        const auto stroke_paint = stroke(stroke_value);
        if (!fill_paint || (stroke_value != nullptr && !stroke_paint)) {
            throw std::invalid_argument("invalid paint");
        }
        return std::make_unique<termin::visual::RectItem2D>(rect, *fill_paint, stroke_paint);
    });
}

bool tc_visual_rect_item2d_set(tc_visual_scene_handle scene,
                               tc_graphic_item_handle item,
                               tc_rect2f rect,
                               tc_visual_fill_paint2d fill_value,
                               const tc_visual_stroke_paint2d* stroke_value) {
    return replace<termin::visual::RectItem2D>(scene, item, "termin.visual.Rect2D", "set RectItem2D", [&] {
        const auto fill_paint = fill(&fill_value);
        const auto stroke_paint = stroke(stroke_value);
        if (!fill_paint || (stroke_value != nullptr && !stroke_paint)) {
            throw std::invalid_argument("invalid paint");
        }
        return std::make_unique<termin::visual::RectItem2D>(rect, *fill_paint, stroke_paint);
    });
}

tc_graphic_item_handle tc_visual_path_item2d_create(tc_visual_scene_handle scene,
                                                    tc_graphic_item_handle parent,
                                                    tc_visual_path2d_view path_value,
                                                    const tc_visual_fill_paint2d* fill_value,
                                                    const tc_visual_stroke_paint2d* stroke_value) {
    return create<termin::visual::PathItem2D>(scene, parent, "create PathItem2D", [&] {
        tgfx::Path2f path;
        const auto fill_paint = fill(fill_value);
        const auto stroke_paint = stroke(stroke_value);
        if (!make_path(path_value, path) || (fill_value != nullptr && !fill_paint) ||
            (stroke_value != nullptr && !stroke_paint)) {
            throw std::invalid_argument("invalid path or paint");
        }
        return std::make_unique<termin::visual::PathItem2D>(std::move(path), fill_paint, stroke_paint);
    });
}

bool tc_visual_path_item2d_set(tc_visual_scene_handle scene,
                               tc_graphic_item_handle item,
                               tc_visual_path2d_view path_value,
                               const tc_visual_fill_paint2d* fill_value,
                               const tc_visual_stroke_paint2d* stroke_value) {
    return replace<termin::visual::PathItem2D>(scene, item, "termin.visual.Path2D", "set PathItem2D", [&] {
        tgfx::Path2f path;
        const auto fill_paint = fill(fill_value);
        const auto stroke_paint = stroke(stroke_value);
        if (!make_path(path_value, path) || (fill_value != nullptr && !fill_paint) ||
            (stroke_value != nullptr && !stroke_paint)) {
            throw std::invalid_argument("invalid path or paint");
        }
        return std::make_unique<termin::visual::PathItem2D>(std::move(path), fill_paint, stroke_paint);
    });
}

tc_graphic_item_handle tc_visual_text_item2d_create(tc_visual_scene_handle scene,
                                                    tc_graphic_item_handle parent,
                                                    const tc_visual_text_desc2d* desc) {
    return create<termin::visual::TextItem2D>(scene, parent, "create TextItem2D", [&] {
        const auto anchor = desc != nullptr ? text_anchor(desc->anchor) : std::nullopt;
        if (desc == nullptr || desc->text == nullptr || desc->font_uri == nullptr || !anchor) {
            throw std::invalid_argument("invalid text descriptor");
        }
        return std::make_unique<termin::visual::TextItem2D>(
            desc->text, desc->font_uri, desc->origin, desc->size_px, authored_color(desc->color), *anchor, desc->layout_bounds);
    });
}

bool tc_visual_text_item2d_set(tc_visual_scene_handle scene,
                               tc_graphic_item_handle item,
                               const tc_visual_text_desc2d* desc) {
    return replace<termin::visual::TextItem2D>(scene, item, "termin.visual.Text2D", "set TextItem2D", [&] {
        const auto anchor = desc != nullptr ? text_anchor(desc->anchor) : std::nullopt;
        if (desc == nullptr || desc->text == nullptr || desc->font_uri == nullptr || !anchor) {
            throw std::invalid_argument("invalid text descriptor");
        }
        return std::make_unique<termin::visual::TextItem2D>(
            desc->text, desc->font_uri, desc->origin, desc->size_px, authored_color(desc->color), *anchor, desc->layout_bounds);
    });
}

tc_graphic_item_handle tc_visual_image_item2d_create(tc_visual_scene_handle scene,
                                                     tc_graphic_item_handle parent,
                                                     const tc_visual_image_desc2d* desc) {
    return create<termin::visual::ImageItem2D>(scene, parent, "create ImageItem2D", [&] {
        const auto mode = desc != nullptr ? sampling(desc->sampling) : std::nullopt;
        if (desc == nullptr || desc->image_uri == nullptr || !mode) {
            throw std::invalid_argument("invalid image descriptor");
        }
        return std::make_unique<termin::visual::ImageItem2D>(
            desc->image_uri, desc->rect, desc->uv, authored_color(desc->tint), *mode);
    });
}

bool tc_visual_image_item2d_set(tc_visual_scene_handle scene,
                                tc_graphic_item_handle item,
                                const tc_visual_image_desc2d* desc) {
    return replace<termin::visual::ImageItem2D>(scene, item, "termin.visual.Image2D", "set ImageItem2D", [&] {
        const auto mode = desc != nullptr ? sampling(desc->sampling) : std::nullopt;
        if (desc == nullptr || desc->image_uri == nullptr || !mode) {
            throw std::invalid_argument("invalid image descriptor");
        }
        return std::make_unique<termin::visual::ImageItem2D>(
            desc->image_uri, desc->rect, desc->uv, authored_color(desc->tint), *mode);
    });
}

tc_graphic_item_handle tc_visual_hit_region_item2d_create(tc_visual_scene_handle scene,
                                                          tc_graphic_item_handle parent,
                                                          tc_visual_path2d_view path_value,
                                                          tc_visual_fill_rule2d rule_value) {
    return create<termin::visual::HitRegionItem2D>(scene, parent, "create HitRegionItem2D", [&] {
        tgfx::Path2f path;
        const auto rule = fill_rule(rule_value);
        if (!rule || !make_path(path_value, path)) {
            throw std::invalid_argument("invalid path or fill rule");
        }
        return std::make_unique<termin::visual::HitRegionItem2D>(std::move(path), *rule);
    });
}

bool tc_visual_hit_region_item2d_set(tc_visual_scene_handle scene,
                                     tc_graphic_item_handle item,
                                     tc_visual_path2d_view path_value,
                                     tc_visual_fill_rule2d rule_value) {
    return replace<termin::visual::HitRegionItem2D>(
        scene, item, "termin.visual.HitRegion2D", "set HitRegionItem2D", [&] {
            tgfx::Path2f path;
            const auto rule = fill_rule(rule_value);
            if (!rule || !make_path(path_value, path)) {
                throw std::invalid_argument("invalid path or fill rule");
            }
            return std::make_unique<termin::visual::HitRegionItem2D>(std::move(path), *rule);
        });
}

} // extern "C"
