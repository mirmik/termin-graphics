#include "termin_visual_scene/scene2d.hpp"

#include "graphic_item_draw_sink_internal.hpp"
#include "scene_composition2d_internal.hpp"
#include "termin_visual_scene/graphic_item2d.hpp"
#include "termin_visual_scene/scene_render2d.hpp"

#include <algorithm>
#include <cmath>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        termin::Bounds2f merged(termin::Bounds2f left, termin::Bounds2f right) {
            return {
                std::min(left.x0, right.x0),
                std::min(left.y0, right.y0),
                std::max(left.x1, right.x1),
                std::max(left.y1, right.y1),
            };
        }

        bool visual_less(const tc_graphic_item* left, const tc_graphic_item* right) {
            if (left->z_order != right->z_order) {
                return left->z_order < right->z_order;
            }
            return left->stable_order < right->stable_order;
        }

        bool close_composition_query(tgfx::CompositionEvaluator2D& composition) {
            while (composition.active() && composition.depth() != 0) {
                if (!composition.pop()) {
                    return false;
                }
            }
            return composition.active() && composition.end_batch();
        }

    } // namespace

    std::optional<GraphicItemHandle>
    TcVisualScene::adopt(tc_graphic_item* item, tc_graphic_item_deleter deleter, tc_graphic_item* parent) {
        if (parent != nullptr && !owns_(*parent)) {
            tc::Log::error("TcVisualScene::adopt rejected foreign parent");
            if (item != nullptr && deleter != nullptr) {
                deleter(item);
            }
            return std::nullopt;
        }
        const auto handle = tc_visual_scene_adopt_item(handle_, item, deleter);
        if (tc_graphic_item_handle_is_invalid(handle)) {
            return std::nullopt;
        }
        if (parent != nullptr && !tc_graphic_item_append_child(parent, item)) {
            tc_visual_scene_destroy_item(handle_, handle);
            return std::nullopt;
        }
        return handle;
    }

    std::optional<GraphicItemHandle> TcVisualScene::adopt(std::unique_ptr<GraphicItem2D> item, GraphicItem2D* parent) {
        if (!item) {
            tc::Log::error("TcVisualScene::adopt received null item");
            return std::nullopt;
        }
        auto* owned = item.release();
        return adopt(
            owned->c_item(), &GraphicItem2D::delete_owned_item, parent != nullptr ? parent->c_item() : nullptr);
    }

    bool
    TcVisualScene::replace(GraphicItemHandle handle, tc_graphic_item* replacement, tc_graphic_item_deleter deleter) {
        return tc_visual_scene_replace_item(handle_, handle, replacement, deleter);
    }

    bool TcVisualScene::replace(GraphicItemHandle handle, std::unique_ptr<GraphicItem2D> replacement) {
        if (!replacement)
            return false;
        auto* owned = replacement.release();
        return replace(handle, owned->c_item(), &GraphicItem2D::delete_owned_item);
    }

    bool TcVisualScene::destroy(GraphicItemHandle handle) {
        return tc_visual_scene_destroy_item(handle_, handle);
    }

    void TcVisualScene::clear() {
        tc_visual_scene_clear(handle_);
    }

    tc_graphic_item* TcVisualScene::resolve(GraphicItemHandle handle) {
        return tc_visual_scene_resolve_item(handle_, handle);
    }

    const tc_graphic_item* TcVisualScene::resolve(GraphicItemHandle handle) const {
        return tc_visual_scene_resolve_item_const(handle_, handle);
    }

    std::vector<tc_graphic_item*> TcVisualScene::items() {
        std::vector<tc_graphic_item*> result(tc_visual_scene_copy_items(handle_, nullptr, 0));
        for (;;) {
            const auto count = tc_visual_scene_copy_items(handle_, result.data(), result.size());
            if (count <= result.size()) {
                result.resize(count);
                return result;
            }
            result.resize(count);
        }
    }

    bool TcVisualScene::owns_(const tc_graphic_item& item) const {
        return item.handle.scene_id == id() && resolve(item.handle) == &item;
    }

    termin::Affine2f TcVisualScene::world_transform(const tc_graphic_item& item) const {
        if (!owns_(item)) {
            tc::Log::error("TcVisualScene::world_transform rejected a foreign item");
            return termin::Affine2f::identity();
        }
        tgfx::CompositionEvaluator2D composition;
        composition.begin_batch();
        if (!detail::push_ancestry(composition, item, false)) {
            composition.abort_batch();
            return termin::Affine2f::identity();
        }
        const termin::Affine2f result = composition.state().local_to_world;
        if (!close_composition_query(composition)) {
            return termin::Affine2f::identity();
        }
        return result;
    }

    bool TcVisualScene::effective_visible(const tc_graphic_item& item) const {
        const tc_graphic_item* cursor = &item;
        while (cursor != nullptr) {
            if (!cursor->visible)
                return false;
            cursor = cursor->parent;
        }
        return true;
    }

    bool TcVisualScene::effective_enabled(const tc_graphic_item& item) const {
        const tc_graphic_item* cursor = &item;
        while (cursor != nullptr) {
            if (!cursor->enabled)
                return false;
            cursor = cursor->parent;
        }
        return true;
    }

    float TcVisualScene::effective_opacity(const tc_graphic_item& item) const {
        float result = 1.0f;
        const tc_graphic_item* cursor = &item;
        while (cursor != nullptr) {
            result *= cursor->opacity;
            cursor = cursor->parent;
        }
        return result;
    }

    bool TcVisualScene::subtree_bounds_(const tc_graphic_item& item,
                                        tgfx::CompositionEvaluator2D& composition,
                                        std::optional<termin::Bounds2f>& out_bounds) const {
        std::optional<termin::Bounds2f> result;
        termin::Bounds2f own{};
        if (item.vtable != nullptr && item.vtable->local_bounds != nullptr && item.vtable->local_bounds(&item, &own)) {
            termin::Bounds2f mapped{};
            if (!composition.map_bounds_to_world(own, mapped)) {
                return false;
            }
            result = mapped;
        }
        for (std::size_t index = 0; index < item.child_count; ++index) {
            const auto* child = item.children[index];
            tgfx::CompositionLayer2D layer;
            if (!detail::composition_layer(*child, layer, false) || !composition.push(layer)) {
                return false;
            }
            std::optional<termin::Bounds2f> child_bounds;
            const bool child_ok = subtree_bounds_(*child, composition, child_bounds);
            if (!composition.pop()) {
                return false;
            }
            if (!child_ok)
                return false;
            if (!child_bounds)
                continue;
            result = result ? std::optional<termin::Bounds2f>(merged(*result, *child_bounds)) : child_bounds;
        }
        out_bounds = result;
        return true;
    }

    std::optional<termin::Bounds2f> TcVisualScene::local_bounds(const tc_graphic_item& item) const {
        if (!owns_(item)) {
            return std::nullopt;
        }
        tgfx::CompositionEvaluator2D composition;
        composition.begin_batch();
        std::optional<termin::Bounds2f> result;
        const bool evaluated = subtree_bounds_(item, composition, result);
        if (!close_composition_query(composition) || !evaluated) {
            return std::nullopt;
        }
        return result;
    }

    std::optional<termin::Bounds2f> TcVisualScene::world_bounds(const tc_graphic_item& item) const {
        if (!owns_(item)) {
            return std::nullopt;
        }
        tgfx::CompositionEvaluator2D composition;
        composition.begin_batch();
        if (!detail::push_ancestry(composition, item, false)) {
            composition.abort_batch();
            return std::nullopt;
        }
        std::optional<termin::Bounds2f> result;
        const bool evaluated = subtree_bounds_(item, composition, result);
        if (!close_composition_query(composition) || !evaluated) {
            return std::nullopt;
        }
        return result;
    }

    GraphicItemDiagnostic2D TcVisualScene::diagnostics(const tc_graphic_item& item) const {
        if (!owns_(item)) {
            return GraphicItemDiagnostic2D::None;
        }
        tgfx::CompositionEvaluator2D composition;
        composition.begin_batch();
        if (!detail::push_ancestry(composition, item, false)) {
            composition.abort_batch();
            return GraphicItemDiagnostic2D::SingularWorldTransform;
        }
        const auto result = composition.state().invertible ? GraphicItemDiagnostic2D::None
                                                           : GraphicItemDiagnostic2D::SingularWorldTransform;
        close_composition_query(composition);
        return result;
    }

    std::vector<tc_graphic_item*> TcVisualScene::sorted_children_(const tc_graphic_item* parent) const {
        std::vector<tc_graphic_item*> result(parent->children, parent->children + parent->child_count);
        std::stable_sort(result.begin(), result.end(), visual_less);
        return result;
    }

    std::vector<tc_graphic_item*> TcVisualScene::sorted_roots_() const {
        auto result = const_cast<TcVisualScene*>(this)->items();
        std::erase_if(result, [](const auto* item) { return item->parent != nullptr; });
        std::stable_sort(result.begin(), result.end(), visual_less);
        return result;
    }

    bool TcVisualScene::paint_item_(const OrderedItem& ordered,
                                    tgfx::CompositionEvaluator2D& composition,
                                    tgfx::DrawList2DBuilder& builder,
                                    SceneRenderResourceResolver2D& resolver) const {
        const tc_graphic_item& item = *ordered.item;
        tgfx::CompositionLayer2D layer;
        if (!detail::composition_layer(item, layer) || !composition.push(layer)) {
            return false;
        }

        bool painted = true;
        if (composition.drawable()) {
            if (item.vtable == nullptr || item.vtable->paint == nullptr) {
                tc::Log::error("graphic item '%s' has no paint method", tc_graphic_item_type_name(&item));
                painted = false;
            } else {
                tc_graphic_item_draw_sink sink{
                    .builder = &builder,
                    .resolver = &resolver,
                    .composition = &composition,
                };
                painted = item.vtable->paint(&item, &sink);
            }
        }
        if (painted && composition.drawable()) {
            for (const OrderedItem& child : ordered.children) {
                if (!paint_item_(child, composition, builder, resolver)) {
                    painted = false;
                    break;
                }
            }
        }
        return painted && composition.pop();
    }

    TcVisualScene::OrderedItem TcVisualScene::build_ordered_item_(tc_graphic_item* item) const {
        OrderedItem result;
        result.item = item;
        auto children = sorted_children_(item);
        result.children.reserve(children.size());
        for (tc_graphic_item* child : children) {
            result.children.push_back(build_ordered_item_(child));
        }
        return result;
    }

    void TcVisualScene::rebuild_order_cache_() const {
        ordered_roots_.clear();
        auto roots = sorted_roots_();
        ordered_roots_.reserve(roots.size());
        for (tc_graphic_item* root : roots) {
            ordered_roots_.push_back(build_ordered_item_(root));
        }
    }

    bool TcVisualScene::paint(tgfx::DrawList2DBuilder& builder, SceneRenderResourceResolver2D& resolver) const {
        const std::uint64_t revision = tc_visual_scene_order_revision(handle_);
        if (revision == 0) {
            tc::Log::error("TcVisualScene::paint rejected stale scene");
            return false;
        }
        if (revision != cached_order_revision_) {
            rebuild_order_cache_();
            cached_order_revision_ = revision;
        }
        tgfx::CompositionEvaluator2D composition;
        composition.begin_batch(&builder);
        for (const OrderedItem& root : ordered_roots_) {
            if (!paint_item_(root, composition, builder, resolver)) {
                composition.abort_batch();
                return false;
            }
        }
        return composition.end_batch();
    }

    bool TcVisualScene::paint_layer_item_(const tc_graphic_item& item,
                                          ScenePaintLayerSink2D& sink,
                                          SceneRenderResourceResolver2D& resolver) const {
        tgfx::DrawList2DBuilder builder;
        tgfx::CompositionEvaluator2D composition;
        composition.begin_batch(&builder);
        if (!detail::push_ancestry(composition, item)) {
            composition.abort_batch();
            return false;
        }
        const std::size_t ancestry_depth = composition.depth();
        bool painted = true;
        if (composition.drawable()) {
            if (item.vtable == nullptr || item.vtable->paint == nullptr) {
                tc::Log::error("graphic item '%s' has no paint method", tc_graphic_item_type_name(&item));
                painted = false;
            } else {
                tc_graphic_item_draw_sink draw_sink{
                    .builder = &builder,
                    .resolver = &resolver,
                    .composition = &composition,
                };
                painted = item.vtable->paint(&item, &draw_sink);
            }
        }
        if (!painted) {
            if (composition.active()) {
                composition.abort_batch();
            }
            return false;
        }
        if (!composition.active() || composition.depth() != ancestry_depth) {
            if (composition.active()) {
                composition.abort_batch();
            }
            tc::Log::error("graphic item '%s' disturbed its composition scopes", tc_graphic_item_type_name(&item));
            return false;
        }
        while (composition.depth() != 0) {
            if (!composition.pop()) {
                return false;
            }
        }
        if (!composition.end_batch()) {
            return false;
        }
        auto draw_list = builder.freeze();
        return draw_list.has_value() && sink.append_item_layer(item, std::move(*draw_list));
    }

    bool TcVisualScene::paint_layers(ScenePaintLayerSink2D& sink, SceneRenderResourceResolver2D& resolver) const {
        const std::uint64_t revision = tc_visual_scene_order_revision(handle_);
        if (revision == 0) {
            tc::Log::error("TcVisualScene::paint_layers rejected stale scene");
            return false;
        }
        if (revision != cached_order_revision_) {
            rebuild_order_cache_();
            cached_order_revision_ = revision;
        }

        std::function<bool(const OrderedItem&, bool)> visit;
        visit = [&](const OrderedItem& ordered, bool ancestors_drawable) {
            const tc_graphic_item& item = *ordered.item;
            const bool drawable = ancestors_drawable && item.visible && item.opacity > 0.0f;
            if (!drawable) {
                return true;
            }
            if (!paint_layer_item_(item, sink, resolver)) {
                return false;
            }
            for (const OrderedItem& child : ordered.children) {
                if (!visit(child, drawable)) {
                    return false;
                }
            }
            return true;
        };
        for (const OrderedItem& root : ordered_roots_) {
            if (!visit(root, true)) {
                return false;
            }
        }
        return true;
    }

    std::size_t TcVisualScene::size() const {
        return tc_visual_scene_item_count(handle_);
    }

    bool TcVisualScene::contains(GraphicItemHandle handle) const {
        return resolve(handle) != nullptr;
    }

    std::uint64_t TcVisualScene::id() const {
        return tc_visual_scene_id(handle_);
    }

} // namespace termin::visual
