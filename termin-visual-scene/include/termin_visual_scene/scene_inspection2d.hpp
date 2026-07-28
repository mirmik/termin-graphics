#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <tcbase/tc_trent.hpp>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/scene2d.hpp"

namespace termin::visual {

inline constexpr std::uint32_t kSceneSerializationVersion2D = 1;

struct SceneItemInspection2D {
    std::uint32_t record_index = 0;
    std::optional<std::uint32_t> parent_index;
    std::vector<std::uint32_t> children;
    std::string type_name;
    std::uint64_t stable_id = 0;
    GraphicItemState2D state;
    GraphicItemPayload2D payload;
    termin::Affine2f world_transform = termin::Affine2f::identity();
    bool effective_visible = true;
    bool effective_enabled = true;
    float effective_opacity = 1.0f;
    std::uint64_t revision = 0;
    std::uint64_t topology_revision = 0;
    std::uint32_t depth = 0;
    GraphicItemDiagnostic2D diagnostics = GraphicItemDiagnostic2D::None;
    std::optional<termin::Bounds2f> local_bounds;
    std::optional<termin::Bounds2f> world_bounds;
};

struct SceneInspection2D {
    std::uint32_t schema_version = kSceneSerializationVersion2D;
    std::uint64_t scene_revision = 0;
    std::vector<SceneItemInspection2D> items;
};

TERMIN_VISUAL_SCENE_API const char* payload_type_name(
    const GraphicItemPayload2D& payload) noexcept;

}  // namespace termin::visual
