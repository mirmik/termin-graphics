#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <tgfx2/composition2d.hpp>

#include "termin_visual_scene/tc_graphic_item.h"

namespace termin::visual::detail {

    inline bool composition_clip(const tc_graphic_item& item, std::optional<tgfx::CompositionClip2D>& out_clip) {
        out_clip.reset();
        if (item.vtable == nullptr || item.vtable->composition_clip == nullptr) {
            return true;
        }
        tc_graphic_item_clip2d_view view{};
        if (!item.vtable->composition_clip(&item, &view)) {
            return true;
        }
        if ((view.verb_count != 0 && view.verbs == nullptr) || (view.point_count != 0 && view.points == nullptr) ||
            view.fill_rule > static_cast<std::uint8_t>(tgfx::FillRule::EvenOdd)) {
            tc::Log::error("graphic item '%s' returned an invalid composition clip view",
                           tc_graphic_item_type_name(&item));
            return false;
        }

        std::vector<tgfx::Path2Verb> verbs;
        verbs.reserve(view.verb_count);
        for (std::size_t index = 0; index < view.verb_count; ++index) {
            if (view.verbs[index] > static_cast<std::uint8_t>(tgfx::Path2Verb::Close)) {
                tc::Log::error("graphic item '%s' returned an unknown composition clip verb",
                               tc_graphic_item_type_name(&item));
                return false;
            }
            verbs.push_back(static_cast<tgfx::Path2Verb>(view.verbs[index]));
        }
        tgfx::Path2f path;
        if (!path.try_assign(verbs, std::span<const termin::Vec2f>{view.points, view.point_count}) || path.empty()) {
            tc::Log::error("graphic item '%s' returned an invalid composition clip path",
                           tc_graphic_item_type_name(&item));
            return false;
        }
        out_clip = tgfx::CompositionClip2D{std::move(path), static_cast<tgfx::FillRule>(view.fill_rule)};
        return true;
    }

    inline bool composition_layer(const tc_graphic_item& item,
                                  tgfx::CompositionLayer2D& out_layer,
                                  bool include_presentation = true) {
        out_layer = {};
        out_layer.transform = item.local_transform;
        if (!include_presentation) {
            return true;
        }
        out_layer.opacity = item.opacity;
        out_layer.visible = item.visible;
        return composition_clip(item, out_layer.clip);
    }

    inline bool push_ancestry(tgfx::CompositionEvaluator2D& evaluator,
                              const tc_graphic_item& item,
                              bool include_presentation = true) {
        std::vector<const tc_graphic_item*> ancestry;
        for (const tc_graphic_item* cursor = &item; cursor != nullptr; cursor = cursor->parent) {
            ancestry.push_back(cursor);
        }
        for (auto iterator = ancestry.rbegin(); iterator != ancestry.rend(); ++iterator) {
            tgfx::CompositionLayer2D layer;
            if (!composition_layer(**iterator, layer, include_presentation) || !evaluator.push(layer)) {
                return false;
            }
        }
        return true;
    }

} // namespace termin::visual::detail
