#include "termin_visual_scene/scene_inspection2d.hpp"

#include <algorithm>
#include <functional>
#include <type_traits>
#include <unordered_map>

#include <tcbase/tc_log.hpp>

namespace termin::visual {

const char* payload_type_name(
    const GraphicItemPayload2D& payload) noexcept {
    return std::visit(
        [](const auto& value) noexcept -> const char* {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, GroupItem2D>) {
                return "termin.visual.Group2D";
            } else if constexpr (std::is_same_v<T, RectItem2D>) {
                return "termin.visual.Rect2D";
            } else if constexpr (std::is_same_v<T, RoundedRectItem2D>) {
                return "termin.visual.RoundedRect2D";
            } else if constexpr (std::is_same_v<T, EllipseItem2D>) {
                return "termin.visual.Ellipse2D";
            } else if constexpr (std::is_same_v<T, PathItem2D>) {
                return "termin.visual.Path2D";
            } else if constexpr (std::is_same_v<T, PolylineItem2D>) {
                return "termin.visual.Polyline2D";
            } else if constexpr (std::is_same_v<T, TextItem2D>) {
                return "termin.visual.Text2D";
            } else if constexpr (std::is_same_v<T, ImageItem2D>) {
                return "termin.visual.Image2D";
            } else if constexpr (std::is_same_v<T, HitRegionItem2D>) {
                return "termin.visual.HitRegion2D";
            } else {
                return "termin.visual.CustomBatch2D";
            }
        },
        payload);
}

SceneInspection2D VisualScene2D::inspection() const {
    std::scoped_lock lock(mutex_);
    SceneInspection2D result;
    result.scene_revision = revision_;
    const auto handles = handles_locked_();
    result.items.reserve(handles.size());

    std::vector<GraphicItemHandle> roots;
    roots.reserve(handles.size());
    for (const auto handle : handles) {
        GraphicItemView topology{};
        if (!storage_.resolve(handle, topology)) {
            tc::Log::error(
                "VisualScene2D::inspection: internal item resolution failed");
            continue;
        }
        if (tc_graphic_item_handle_is_invalid(topology.parent)) {
            roots.push_back(handle);
        }
    }
    std::stable_sort(
        roots.begin(),
        roots.end(),
        [&](GraphicItemHandle lhs, GraphicItemHandle rhs) {
            const auto* left = item_locked_(lhs);
            const auto* right = item_locked_(rhs);
            return left != nullptr && right != nullptr &&
                   left->stable_order < right->stable_order;
        });

    std::unordered_map<std::uint32_t, std::uint32_t> record_indices;
    std::function<void(GraphicItemHandle, std::optional<std::uint32_t>)>
        append_subtree;
    append_subtree = [&](GraphicItemHandle handle,
                         std::optional<std::uint32_t> parent_index) {
        GraphicItemSnapshot2D snapshot;
        GraphicItemView topology{};
        if (!snapshot_locked_(handle, snapshot)
            || !storage_.resolve(handle, topology)) {
            tc::Log::error(
                "VisualScene2D::inspection: internal snapshot failed");
            return;
        }

        const std::uint32_t record_index =
            static_cast<std::uint32_t>(result.items.size());
        record_indices.emplace(handle.index, record_index);
        result.items.push_back(SceneItemInspection2D{
            record_index,
            parent_index,
            {},
            payload_type_name(snapshot.payload),
            snapshot.stable_order,
            snapshot.state,
            snapshot.payload,
            snapshot.world_transform,
            snapshot.effective_visible,
            snapshot.effective_enabled,
            snapshot.effective_opacity,
            snapshot.revision,
            snapshot.topology_revision,
            snapshot.depth,
            snapshot.diagnostics,
            snapshot.local_bounds,
            snapshot.world_bounds,
        });

        GraphicItemHandle child = topology.first_child;
        while (!tc_graphic_item_handle_is_invalid(child)) {
            GraphicItemView child_topology{};
            if (!storage_.resolve(child, child_topology)) {
                tc::Log::error(
                    "VisualScene2D::inspection: child resolution failed");
                return;
            }
            append_subtree(child, record_index);
            const auto found = record_indices.find(child.index);
            if (found != record_indices.end()) {
                result.items[record_index].children.push_back(found->second);
            }
            child = child_topology.next_sibling;
        }
    };

    for (const auto root : roots) {
        append_subtree(root, std::nullopt);
    }
    return result;
}

}  // namespace termin::visual
