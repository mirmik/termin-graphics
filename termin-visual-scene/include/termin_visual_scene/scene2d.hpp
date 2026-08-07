#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <termin/geom/affine2.hpp>
#include <termin/geom/bounds2.hpp>
#include <termin/geom/vec2.hpp>
#include <tgfx2/draw_list2d.hpp>
#include <tgfx2/path2d.hpp>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/tc_visual_scene.h"

namespace termin::visual {

class GraphicItem2D;
class SceneRenderResourceResolver2D;
class TcVisualScene;
using GraphicItemHandle = tc_graphic_item_handle;

TERMIN_VISUAL_SCENE_API
std::optional<GraphicItemHandle> hit_test(
    const TcVisualScene& scene,
    termin::Vec2f world_point);

struct GeometricClip2D {
    tgfx::Path2f path;
    tgfx::FillRule rule = tgfx::FillRule::NonZero;
};

enum class GraphicItemDiagnostic2D : std::uint32_t {
    None = 0,
    SingularWorldTransform = 1u << 0,
};

// Copyable, non-owning facade over a pooled tc_visual_scene handle. Creation
// and destruction are explicit tc_visual_scene_create/destroy operations.
class TERMIN_VISUAL_SCENE_API TcVisualScene {
public:
    TcVisualScene() = default;
    explicit TcVisualScene(tc_visual_scene_handle scene)
        : handle_(scene) {}
    ~TcVisualScene() = default;

    TcVisualScene(const TcVisualScene&) = default;
    TcVisualScene& operator=(const TcVisualScene&) = default;

    std::optional<GraphicItemHandle> adopt(
        tc_graphic_item* item,
        tc_graphic_item_deleter deleter,
        tc_graphic_item* parent = nullptr);
    std::optional<GraphicItemHandle> adopt(
        std::unique_ptr<GraphicItem2D> item,
        GraphicItem2D* parent = nullptr);
    bool replace(
        GraphicItemHandle handle,
        tc_graphic_item* replacement,
        tc_graphic_item_deleter deleter);
    bool replace(
        GraphicItemHandle handle,
        std::unique_ptr<GraphicItem2D> replacement);
    bool destroy(GraphicItemHandle handle);
    void clear();

    tc_graphic_item* resolve(GraphicItemHandle handle);
    const tc_graphic_item* resolve(
        GraphicItemHandle handle) const;
    std::vector<tc_graphic_item*> items();

    termin::Affine2f world_transform(
        const tc_graphic_item& item) const;
    bool effective_visible(
        const tc_graphic_item& item) const;
    bool effective_enabled(
        const tc_graphic_item& item) const;
    float effective_opacity(
        const tc_graphic_item& item) const;
    std::optional<termin::Bounds2f> local_bounds(
        const tc_graphic_item& item) const;
    std::optional<termin::Bounds2f> world_bounds(
        const tc_graphic_item& item) const;
    GraphicItemDiagnostic2D diagnostics(
        const tc_graphic_item& item) const;

    // Appends this scene immediately to the caller's current draw-list build.
    // No render snapshot or retained command copy is created by the scene.
    bool paint(
        tgfx::DrawList2DBuilder& builder,
        SceneRenderResourceResolver2D& resolver) const;

    std::size_t size() const;
    bool contains(GraphicItemHandle handle) const;
    std::uint64_t id() const;
    tc_visual_scene_handle handle() const {
        return handle_;
    }
    bool valid() const {
        return tc_visual_scene_is_valid(handle_);
    }

private:
    struct OrderedItem {
        tc_graphic_item* item = nullptr;
        std::vector<OrderedItem> children;
    };

    bool owns_(const tc_graphic_item& item) const;
    std::vector<tc_graphic_item*> sorted_children_(
        const tc_graphic_item* parent) const;
    std::vector<tc_graphic_item*> sorted_roots_() const;
    void rebuild_order_cache_() const;
    OrderedItem build_ordered_item_(tc_graphic_item* item) const;
    bool paint_item_(
        const OrderedItem& ordered,
        tgfx::DrawList2DBuilder& builder,
        SceneRenderResourceResolver2D& resolver) const;
    std::optional<termin::Bounds2f> subtree_bounds_(
        const tc_graphic_item& item) const;

    tc_visual_scene_handle handle_ =
        tc_visual_scene_handle_invalid();
    mutable std::uint64_t cached_order_revision_ = 0;
    mutable std::vector<OrderedItem> ordered_roots_;

    friend TERMIN_VISUAL_SCENE_API
    std::optional<GraphicItemHandle> hit_test(
        const TcVisualScene&,
        termin::Vec2f);
};

}  // namespace termin::visual
