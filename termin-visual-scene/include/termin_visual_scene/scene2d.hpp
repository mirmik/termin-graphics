#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <termin/geom/affine2.hpp>
#include <termin/geom/bounds2.hpp>
#include <termin/geom/rect2.hpp>
#include <termin/geom/vec2.hpp>
#include <tgfx2/draw_list2d.hpp>
#include <tgfx2/path2d.hpp>
#include <tcbase/tc_trent.hpp>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/visual_scene.hpp"

namespace termin::visual {

class SceneRenderResourceResolver2D;
class SceneRenderSnapshot2D;
struct SceneInspection2D;

// Serializable resource identity. Runtime FontHandle/TextureHandle values are
// produced only while preparing a render snapshot.
struct StableResourceRef2D {
    std::string uri;

    bool valid() const noexcept { return !uri.empty(); }
    friend bool operator==(const StableResourceRef2D&, const StableResourceRef2D&) = default;
};

struct GeometricClip2D {
    tgfx::Path2f path;
    tgfx::FillRule rule = tgfx::FillRule::NonZero;
};

struct GroupItem2D {};

struct RectItem2D {
    termin::Rect2f rect{};
    tgfx::FillPaint fill{};
    std::optional<tgfx::StrokePaint> stroke;
};

struct RoundedRectItem2D {
    termin::Rect2f rect{};
    float radius = 0.0f;
    tgfx::FillPaint fill{};
    std::optional<tgfx::StrokePaint> stroke;
};

struct EllipseItem2D {
    termin::Rect2f bounds{};
    tgfx::FillPaint fill{};
    std::optional<tgfx::StrokePaint> stroke;
};

struct PathItem2D {
    tgfx::Path2f path;
    std::optional<tgfx::FillPaint> fill;
    std::optional<tgfx::StrokePaint> stroke;
};

struct PolylineItem2D {
    std::vector<termin::Vec2f> points;
    tgfx::StrokePaint stroke{};
    bool closed = false;
};

struct TextItem2D {
    std::string text;
    StableResourceRef2D font;
    termin::Vec2f origin{};
    float size_px = 14.0f;
    tgfx::Color4f color{};
    tgfx::TextAnchor2D anchor = tgfx::TextAnchor2D::Left;
    // Text shaping belongs to the host/resource layer. This serializable
    // geometry is the deterministic local bound used before resolution.
    termin::Bounds2f layout_bounds{};
};

struct ImageItem2D {
    StableResourceRef2D image;
    termin::Rect2f rect{};
    termin::Rect2f uv{0.0f, 0.0f, 1.0f, 1.0f};
    tgfx::Color4f tint{};
    tgfx::DrawTextureSampling2D sampling =
        tgfx::DrawTextureSampling2D::Linear;
};

struct HitRegionItem2D {
    tgfx::Path2f path;
    tgfx::FillRule rule = tgfx::FillRule::NonZero;
};

// Custom producers retain their data outside this scene. The key is stable
// persistent identity; the host resolves it while preparing a snapshot.
struct CustomBatchItem2D {
    std::string key;
    termin::Bounds2f local_bounds{};
};

using GraphicItemPayload2D = std::variant<
    GroupItem2D,
    RectItem2D,
    RoundedRectItem2D,
    EllipseItem2D,
    PathItem2D,
    PolylineItem2D,
    TextItem2D,
    ImageItem2D,
    HitRegionItem2D,
    CustomBatchItem2D>;

struct GraphicItemState2D {
    termin::Affine2f local_transform = termin::Affine2f::identity();
    bool visible = true;
    bool enabled = true;
    float opacity = 1.0f;
    std::int64_t z_order = 0;
    std::optional<GeometricClip2D> clip;
};

enum class GraphicItemDiagnostic2D : std::uint32_t {
    None = 0,
    SingularWorldTransform = 1u << 0,
};

struct GraphicItemSnapshot2D {
    GraphicItemHandle handle = tc_graphic_item_handle_invalid();
    GraphicItemHandle parent = tc_graphic_item_handle_invalid();
    GraphicItemState2D state;
    GraphicItemPayload2D payload;
    termin::Affine2f world_transform = termin::Affine2f::identity();
    bool effective_visible = true;
    bool effective_enabled = true;
    float effective_opacity = 1.0f;
    std::uint64_t stable_order = 0;
    std::uint64_t revision = 0;
    std::uint64_t topology_revision = 0;
    std::uint32_t depth = 0;
    GraphicItemDiagnostic2D diagnostics = GraphicItemDiagnostic2D::None;
    std::optional<termin::Bounds2f> local_bounds;
    std::optional<termin::Bounds2f> world_bounds;
    std::vector<GeometricClip2D> effective_clips;
};

class TERMIN_VISUAL_SCENE_API VisualScene2D {
public:
    VisualScene2D() = default;
    ~VisualScene2D() = default;

    VisualScene2D(const VisualScene2D&) = delete;
    VisualScene2D& operator=(const VisualScene2D&) = delete;
    VisualScene2D(VisualScene2D&&) = delete;
    VisualScene2D& operator=(VisualScene2D&&) = delete;

    std::optional<GraphicItemHandle> create(
        GraphicItemPayload2D payload = GroupItem2D{},
        GraphicItemHandle parent = tc_graphic_item_handle_invalid());
    bool destroy_leaf(GraphicItemHandle item);
    bool destroy_subtree(GraphicItemHandle item);
    bool reparent(GraphicItemHandle item, GraphicItemHandle parent);
    bool detach(GraphicItemHandle item);
    void clear();

    // Validation happens before the lock-protected commit. Failed mutations
    // leave both state and revision unchanged.
    bool set_state(GraphicItemHandle item, GraphicItemState2D state);
    bool set_payload(GraphicItemHandle item, GraphicItemPayload2D payload);
    bool set_item(
        GraphicItemHandle item,
        GraphicItemState2D state,
        GraphicItemPayload2D payload);

    std::optional<GraphicItemSnapshot2D> snapshot(GraphicItemHandle item) const;
    std::vector<GraphicItemSnapshot2D> snapshots() const;
    SceneInspection2D inspection() const;
    tc::trent serialize() const;

    // Restore accepts only the current versioned, handle-free schema and only
    // into an empty scene. Parsing and construction happen off-scene; failure
    // leaves the destination empty and unchanged.
    bool restore(const tc::trent& serialized);
    std::optional<SceneRenderSnapshot2D> prepare_render_snapshot(
        SceneRenderResourceResolver2D& resolver) const;

    std::size_t size() const;
    bool contains(GraphicItemHandle item) const;
    std::uint64_t revision() const;
    std::uint64_t id() const;

private:
    bool owns_locked_(GraphicItemHandle item) const;
    tc_graphic_item* item_locked_(GraphicItemHandle item) const;
    std::vector<GraphicItemHandle> handles_locked_() const;
    bool payload_locked_(
        GraphicItemHandle item,
        GraphicItemPayload2D& out) const;
    bool state_locked_(
        GraphicItemHandle item,
        GraphicItemState2D& out) const;
    bool replace_payload_locked_(
        GraphicItemHandle item,
        GraphicItemPayload2D payload,
        std::optional<GeometricClip2D> clip);
    bool restore_metadata_locked_(
        GraphicItemHandle item,
        std::uint64_t stable_order,
        std::uint64_t revision,
        std::uint64_t topology_revision);
    bool snapshot_locked_(
        GraphicItemHandle item,
        GraphicItemSnapshot2D& out) const;
    std::vector<GraphicItemSnapshot2D> snapshots_locked_() const;
    std::optional<termin::Bounds2f> subtree_local_bounds_locked_(
        GraphicItemHandle root) const;

    mutable std::mutex mutex_;
    mutable VisualSceneStorage storage_;
    std::uint64_t revision_ = 0;
};

}  // namespace termin::visual
