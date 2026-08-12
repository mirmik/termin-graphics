#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
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

namespace tgfx {
    class CompositionEvaluator2D;
}

namespace termin::visual {

    class GraphicItem2D;
    class SceneRenderResourceResolver2D;
    class ScenePaintLayerSink2D;
    class TcVisualScene;
    using GraphicItemHandle = tc_graphic_item_handle;

    TERMIN_VISUAL_SCENE_API
    std::optional<GraphicItemHandle> hit_test(const TcVisualScene& scene, termin::Vec2f world_point);

    struct GeometricClip2D {
        tgfx::Path2f path;
        tgfx::FillRule rule = tgfx::FillRule::NonZero;
    };

    // Receives balanced, self-contained draw lists for individual item paint
    // slots in canonical scene order. Cross-tree composition adapters can
    // append foreign semantic content after a slot without teaching the scene
    // about that content's ownership or paint vocabulary.
    class TERMIN_VISUAL_SCENE_API ScenePaintLayerSink2D {
    public:
        virtual ~ScenePaintLayerSink2D() = default;
        virtual bool append_item_layer(const tc_graphic_item& item, tgfx::DrawList2D draw_list) = 0;
    };

    using SceneHitLayerVisitor2D =
        std::function<bool(const tc_graphic_item& item, termin::Vec2f local_point, bool item_hit)>;

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

        std::optional<GraphicItemHandle>
        adopt(tc_graphic_item* item, tc_graphic_item_deleter deleter, tc_graphic_item* parent = nullptr);
        std::optional<GraphicItemHandle> adopt(std::unique_ptr<GraphicItem2D> item, GraphicItem2D* parent = nullptr);
        bool replace(GraphicItemHandle handle, tc_graphic_item* replacement, tc_graphic_item_deleter deleter);
        bool replace(GraphicItemHandle handle, std::unique_ptr<GraphicItem2D> replacement);
        bool destroy(GraphicItemHandle handle);
        void clear();

        tc_graphic_item* resolve(GraphicItemHandle handle);
        const tc_graphic_item* resolve(GraphicItemHandle handle) const;
        std::vector<tc_graphic_item*> items();

        termin::Affine2f world_transform(const tc_graphic_item& item) const;
        bool effective_visible(const tc_graphic_item& item) const;
        bool effective_enabled(const tc_graphic_item& item) const;
        float effective_opacity(const tc_graphic_item& item) const;
        std::optional<termin::Bounds2f> local_bounds(const tc_graphic_item& item) const;
        std::optional<termin::Bounds2f> world_bounds(const tc_graphic_item& item) const;
        GraphicItemDiagnostic2D diagnostics(const tc_graphic_item& item) const;

        // Appends this scene immediately to the caller's current draw-list build.
        // No render snapshot or retained command copy is created by the scene.
        bool paint(tgfx::DrawList2DBuilder& builder, SceneRenderResourceResolver2D& resolver) const;
        // Emits one balanced layer per item. The sink is called after the
        // item's own paint and before its ordered children, matching the
        // ordinary painter traversal exactly.
        bool paint_layers(ScenePaintLayerSink2D& sink, SceneRenderResourceResolver2D& resolver) const;

        // Visits eligible layers in reverse painter order. Children are
        // visited before their parent's own paint slot. Returning true from
        // the visitor stops traversal. Clip, visibility, enabled state and
        // exact inverse affine mapping match ordinary scene hit testing.
        bool visit_hit_layers(termin::Vec2f world_point, const SceneHitLayerVisitor2D& visitor) const;

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
        std::vector<tc_graphic_item*> sorted_children_(const tc_graphic_item* parent) const;
        std::vector<tc_graphic_item*> sorted_roots_() const;
        void rebuild_order_cache_() const;
        OrderedItem build_ordered_item_(tc_graphic_item* item) const;
        bool paint_item_(const OrderedItem& ordered,
                         tgfx::CompositionEvaluator2D& composition,
                         tgfx::DrawList2DBuilder& builder,
                         SceneRenderResourceResolver2D& resolver) const;
        bool paint_layer_item_(const tc_graphic_item& item,
                               ScenePaintLayerSink2D& sink,
                               SceneRenderResourceResolver2D& resolver) const;
        bool subtree_bounds_(const tc_graphic_item& item,
                             tgfx::CompositionEvaluator2D& composition,
                             std::optional<termin::Bounds2f>& out_bounds) const;

        tc_visual_scene_handle handle_ = tc_visual_scene_handle_invalid();
        mutable std::uint64_t cached_order_revision_ = 0;
        mutable std::vector<OrderedItem> ordered_roots_;

        friend TERMIN_VISUAL_SCENE_API std::optional<GraphicItemHandle> hit_test(const TcVisualScene&, termin::Vec2f);
    };

} // namespace termin::visual
