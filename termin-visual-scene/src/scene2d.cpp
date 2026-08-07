#include "termin_visual_scene/scene2d.hpp"

#include "graphic_item_draw_sink_internal.hpp"
#include "termin_visual_scene/graphic_item2d.hpp"
#include "termin_visual_scene/scene_render2d.hpp"

#include <algorithm>
#include <cmath>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
namespace {

termin::Bounds2f merged(
    termin::Bounds2f left,
    termin::Bounds2f right)
{
    return {
        std::min(left.x0, right.x0),
        std::min(left.y0, right.y0),
        std::max(left.x1, right.x1),
        std::max(left.y1, right.y1),
    };
}

bool visual_less(
    const tc_graphic_item* left,
    const tc_graphic_item* right)
{
    if (left->z_order != right->z_order) {
        return left->z_order < right->z_order;
    }
    return left->stable_order < right->stable_order;
}

bool is_identity(const termin::Affine2f& value)
{
    return value.m00 == 1.0f && value.m01 == 0.0f &&
        value.m10 == 0.0f && value.m11 == 1.0f &&
        value.tx == 0.0f && value.ty == 0.0f;
}

}  // namespace

std::optional<GraphicItemHandle> TcVisualScene::adopt(
    tc_graphic_item* item,
    tc_graphic_item_deleter deleter,
    tc_graphic_item* parent)
{
    if (parent != nullptr && !owns_(*parent)) {
        tc::Log::error(
            "TcVisualScene::adopt rejected foreign parent");
        if (item != nullptr && deleter != nullptr) {
            deleter(item);
        }
        return std::nullopt;
    }
    const auto handle =
        tc_visual_scene_adopt_item(handle_, item, deleter);
    if (tc_graphic_item_handle_is_invalid(handle)) {
        return std::nullopt;
    }
    if (parent != nullptr &&
        !tc_graphic_item_append_child(parent, item)) {
        tc_visual_scene_destroy_item(handle_, handle);
        return std::nullopt;
    }
    return handle;
}

std::optional<GraphicItemHandle> TcVisualScene::adopt(
    std::unique_ptr<GraphicItem2D> item,
    GraphicItem2D* parent)
{
    if (!item) {
        tc::Log::error(
            "TcVisualScene::adopt received null item");
        return std::nullopt;
    }
    auto* owned = item.release();
    return adopt(
        owned->c_item(),
        &GraphicItem2D::delete_owned_item,
        parent != nullptr ? parent->c_item() : nullptr);
}

bool TcVisualScene::replace(
    GraphicItemHandle handle,
    tc_graphic_item* replacement,
    tc_graphic_item_deleter deleter)
{
    return tc_visual_scene_replace_item(
        handle_, handle, replacement, deleter);
}

bool TcVisualScene::replace(
    GraphicItemHandle handle,
    std::unique_ptr<GraphicItem2D> replacement)
{
    if (!replacement) return false;
    auto* owned = replacement.release();
    return replace(
        handle,
        owned->c_item(),
        &GraphicItem2D::delete_owned_item);
}

bool TcVisualScene::destroy(
    GraphicItemHandle handle)
{
    return tc_visual_scene_destroy_item(handle_, handle);
}

void TcVisualScene::clear() {
    tc_visual_scene_clear(handle_);
}

tc_graphic_item* TcVisualScene::resolve(
    GraphicItemHandle handle)
{
    return tc_visual_scene_resolve_item(handle_, handle);
}

const tc_graphic_item* TcVisualScene::resolve(
    GraphicItemHandle handle) const
{
    return tc_visual_scene_resolve_item_const(handle_, handle);
}

std::vector<tc_graphic_item*> TcVisualScene::items() {
    std::vector<tc_graphic_item*> result(
        tc_visual_scene_copy_items(handle_, nullptr, 0));
    for (;;) {
        const auto count = tc_visual_scene_copy_items(
            handle_, result.data(), result.size());
        if (count <= result.size()) {
            result.resize(count);
            return result;
        }
        result.resize(count);
    }
}

bool TcVisualScene::owns_(
    const tc_graphic_item& item) const
{
    return item.handle.scene_id == id() &&
        resolve(item.handle) == &item;
}

termin::Affine2f TcVisualScene::world_transform(
    const tc_graphic_item& item) const
{
    termin::Affine2f result =
        termin::Affine2f::identity();
    std::vector<const tc_graphic_item*> ancestry;
    const tc_graphic_item* cursor = &item;
    while (cursor != nullptr) {
        ancestry.push_back(cursor);
        cursor = cursor->parent;
    }
    for (auto iterator = ancestry.rbegin();
         iterator != ancestry.rend();
         ++iterator) {
        result = result * (*iterator)->local_transform;
    }
    return result;
}

bool TcVisualScene::effective_visible(
    const tc_graphic_item& item) const
{
    const tc_graphic_item* cursor = &item;
    while (cursor != nullptr) {
        if (!cursor->visible) return false;
        cursor = cursor->parent;
    }
    return true;
}

bool TcVisualScene::effective_enabled(
    const tc_graphic_item& item) const
{
    const tc_graphic_item* cursor = &item;
    while (cursor != nullptr) {
        if (!cursor->enabled) return false;
        cursor = cursor->parent;
    }
    return true;
}

float TcVisualScene::effective_opacity(
    const tc_graphic_item& item) const
{
    float result = 1.0f;
    const tc_graphic_item* cursor = &item;
    while (cursor != nullptr) {
        result *= cursor->opacity;
        cursor = cursor->parent;
    }
    return result;
}

std::optional<termin::Bounds2f>
TcVisualScene::subtree_bounds_(
    const tc_graphic_item& item) const
{
    std::optional<termin::Bounds2f> result;
    termin::Bounds2f own{};
    if (item.vtable != nullptr &&
        item.vtable->local_bounds != nullptr &&
        item.vtable->local_bounds(&item, &own)) {
        result = own;
    }
    for (std::size_t index = 0;
         index < item.child_count;
         ++index) {
        const auto* child = item.children[index];
        const auto child_bounds = subtree_bounds_(*child);
        if (!child_bounds) continue;
        const auto transformed =
            child->local_transform.transform_bounds(
                *child_bounds);
        result = result
            ? std::optional<termin::Bounds2f>(
                  merged(*result, transformed))
            : std::optional<termin::Bounds2f>(
                  transformed);
    }
    return result;
}

std::optional<termin::Bounds2f>
TcVisualScene::local_bounds(
    const tc_graphic_item& item) const
{
    return owns_(item)
        ? subtree_bounds_(item)
        : std::nullopt;
}

std::optional<termin::Bounds2f>
TcVisualScene::world_bounds(
    const tc_graphic_item& item) const
{
    const auto bounds = local_bounds(item);
    return bounds
        ? std::optional<termin::Bounds2f>(
              world_transform(item)
                  .transform_bounds(*bounds))
        : std::nullopt;
}

GraphicItemDiagnostic2D TcVisualScene::diagnostics(
    const tc_graphic_item& item) const
{
    return std::abs(
        world_transform(item).determinant()) <= 1e-8f
        ? GraphicItemDiagnostic2D::SingularWorldTransform
        : GraphicItemDiagnostic2D::None;
}

std::vector<tc_graphic_item*>
TcVisualScene::sorted_children_(
    const tc_graphic_item* parent) const
{
    std::vector<tc_graphic_item*> result(
        parent->children,
        parent->children + parent->child_count);
    std::stable_sort(
        result.begin(), result.end(), visual_less);
    return result;
}

std::vector<tc_graphic_item*>
TcVisualScene::sorted_roots_() const {
    auto result = const_cast<TcVisualScene*>(this)->items();
    std::erase_if(
        result,
        [](const auto* item) {
            return item->parent != nullptr;
        });
    std::stable_sort(
        result.begin(), result.end(), visual_less);
    return result;
}

bool TcVisualScene::paint_item_(
    const OrderedItem& ordered,
    tgfx::DrawList2DBuilder& builder,
    SceneRenderResourceResolver2D& resolver) const
{
    const tc_graphic_item& item = *ordered.item;
    if (!item.visible || item.opacity <= 0.0f) {
        return true;
    }
    if (item.vtable == nullptr ||
        item.vtable->paint == nullptr) {
        tc::Log::error(
            "graphic item '%s' has no paint method",
            tc_graphic_item_type_name(&item));
        return false;
    }

    const bool transform_pushed = !is_identity(item.local_transform);
    if (transform_pushed &&
        !builder.push_transform(item.local_transform)) {
        return false;
    }
    const bool opacity_pushed = item.opacity != 1.0f;
    if (opacity_pushed && !builder.push_opacity(item.opacity)) {
        if (transform_pushed) builder.pop_transform();
        return false;
    }
    tc_graphic_item_draw_sink sink{
        .builder = &builder,
        .resolver = &resolver,
    };
    bool clip_pushed = false;
    if (item.vtable->push_clip != nullptr &&
        !item.vtable->push_clip(
            &item, &sink, &clip_pushed)) {
        if (opacity_pushed) builder.pop_opacity();
        if (transform_pushed) builder.pop_transform();
        return false;
    }

    bool painted = item.vtable->paint(&item, &sink);
    if (painted) {
        for (const OrderedItem& child : ordered.children) {
            if (!paint_item_(child, builder, resolver)) {
                painted = false;
                break;
            }
        }
    }
    const bool clip_popped =
        !clip_pushed || builder.pop_clip();
    const bool opacity_popped =
        !opacity_pushed || builder.pop_opacity();
    const bool transform_popped =
        !transform_pushed || builder.pop_transform();
    return painted && clip_popped
        && opacity_popped && transform_popped;
}

TcVisualScene::OrderedItem TcVisualScene::build_ordered_item_(
    tc_graphic_item* item) const
{
    OrderedItem result;
    result.item = item;
    auto children = sorted_children_(item);
    result.children.reserve(children.size());
    for (tc_graphic_item* child : children) {
        result.children.push_back(build_ordered_item_(child));
    }
    return result;
}

void TcVisualScene::rebuild_order_cache_() const
{
    ordered_roots_.clear();
    auto roots = sorted_roots_();
    ordered_roots_.reserve(roots.size());
    for (tc_graphic_item* root : roots) {
        ordered_roots_.push_back(build_ordered_item_(root));
    }
}

bool TcVisualScene::paint(
    tgfx::DrawList2DBuilder& builder,
    SceneRenderResourceResolver2D& resolver) const
{
    const std::uint64_t revision =
        tc_visual_scene_order_revision(handle_);
    if (revision == 0) {
        tc::Log::error("TcVisualScene::paint rejected stale scene");
        return false;
    }
    if (revision != cached_order_revision_) {
        rebuild_order_cache_();
        cached_order_revision_ = revision;
    }
    for (const OrderedItem& root : ordered_roots_) {
        if (!paint_item_(root, builder, resolver)) {
            return false;
        }
    }
    return true;
}

std::size_t TcVisualScene::size() const {
    return tc_visual_scene_item_count(handle_);
}

bool TcVisualScene::contains(
    GraphicItemHandle handle) const
{
    return resolve(handle) != nullptr;
}

std::uint64_t TcVisualScene::id() const {
    return tc_visual_scene_id(handle_);
}

}  // namespace termin::visual
