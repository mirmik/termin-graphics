#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <termin/gui_native/signal.hpp>
#include <termin/gui_native/tc_ui_document.h>
#include <termin_visual_scene/scene2d.hpp>

namespace termin::gui_native {

struct SceneTransform {
    float origin_x = 0.0f;
    float origin_y = 0.0f;
    float zoom = 1.0f;

    tc_ui_point world_to_screen(tc_ui_point point) const;
    tc_ui_point screen_to_world(tc_ui_point point) const;
};

class GraphicsScene;

// A cheap scene-plus-generation-handle value. It never owns an item and every
// operation validates the handle through VisualScene2D.
class GraphicItemRef {
public:
    GraphicItemRef() = default;

    bool valid() const;
    explicit operator bool() const { return valid(); }
    termin::visual::GraphicItemHandle handle() const { return handle_; }

    std::string stable_id() const;
    bool set_stable_id(std::string stable_id);
    std::optional<GraphicItemRef> parent() const;
    std::vector<GraphicItemRef> children() const;
    bool reparent(const std::optional<GraphicItemRef>& parent);

    tc_ui_point position() const;
    bool set_position(tc_ui_point position);
    tc_ui_size size() const;
    bool set_size(tc_ui_size size);
    std::int64_t z_order() const;
    bool set_z_order(std::int64_t z_order);
    bool visible() const;
    bool set_visible(bool visible);
    bool enabled() const;
    bool set_enabled(bool enabled);
    bool selectable() const;
    bool set_selectable(bool selectable);
    bool draggable() const;
    bool set_draggable(bool draggable);
    tc_ui_point world_position() const;
    tc_ui_rect world_bounds() const;

    bool set_polyline(
        std::vector<termin::Vec2f> points,
        tgfx::StrokePaint stroke,
        bool closed = false);

    friend bool operator==(
        const GraphicItemRef& left,
        const GraphicItemRef& right) {
        return left.scene_ == right.scene_ &&
               left.handle_.scene_id == right.handle_.scene_id &&
               left.handle_.index == right.handle_.index &&
               left.handle_.generation == right.handle_.generation;
    }

private:
    GraphicItemRef(
        GraphicsScene* scene,
        std::weak_ptr<void> lifetime,
        termin::visual::GraphicItemHandle handle)
        : scene_(scene),
          lifetime_(std::move(lifetime)),
          handle_(handle) {}

    GraphicsScene* scene_ = nullptr;
    std::weak_ptr<void> lifetime_;
    termin::visual::GraphicItemHandle handle_ =
        tc_graphic_item_handle_invalid();

    friend class GraphicsScene;
    friend class SceneView;
};

// GUI-facing metadata and invalidation around the one canonical VisualScene2D
// storage. Topology, transforms, payloads and hit testing all stay in the
// termin-visual-scene implementation.
class GraphicsScene : public std::enable_shared_from_this<GraphicsScene> {
public:
    GraphicsScene() = default;
    ~GraphicsScene() = default;

    GraphicsScene(const GraphicsScene&) = delete;
    GraphicsScene& operator=(const GraphicsScene&) = delete;

    GraphicItemRef create_group(
        std::string stable_id = {},
        std::optional<GraphicItemRef> parent = std::nullopt);
    GraphicItemRef create_rect(
        std::string stable_id,
        termin::Rect2f rect,
        tgfx::FillPaint fill,
        std::optional<tgfx::StrokePaint> stroke = std::nullopt,
        std::optional<GraphicItemRef> parent = std::nullopt);
    GraphicItemRef create_rounded_rect(
        std::string stable_id,
        termin::Rect2f rect,
        float radius,
        tgfx::FillPaint fill,
        std::optional<tgfx::StrokePaint> stroke = std::nullopt,
        std::optional<GraphicItemRef> parent = std::nullopt);
    GraphicItemRef create_ellipse(
        std::string stable_id,
        termin::Rect2f bounds,
        tgfx::FillPaint fill,
        std::optional<tgfx::StrokePaint> stroke = std::nullopt,
        std::optional<GraphicItemRef> parent = std::nullopt);
    GraphicItemRef create_path(
        std::string stable_id,
        tgfx::Path2f path,
        std::optional<tgfx::FillPaint> fill,
        std::optional<tgfx::StrokePaint> stroke = std::nullopt,
        std::optional<GraphicItemRef> parent = std::nullopt);
    GraphicItemRef create_polyline(
        std::string stable_id,
        std::vector<termin::Vec2f> points,
        tgfx::StrokePaint stroke,
        bool closed = false,
        std::optional<GraphicItemRef> parent = std::nullopt);
    GraphicItemRef create_text(
        std::string stable_id,
        std::string text,
        termin::Vec2f origin,
        float size_px,
        tgfx::Color4f color,
        termin::Bounds2f layout_bounds,
        std::optional<GraphicItemRef> parent = std::nullopt);
    GraphicItemRef create_hit_region(
        std::string stable_id,
        tgfx::Path2f path,
        std::optional<GraphicItemRef> parent = std::nullopt);

    bool destroy(const GraphicItemRef& item);
    void clear();
    std::optional<GraphicItemRef> item(
        termin::visual::GraphicItemHandle handle);
    std::vector<GraphicItemRef> items();
    std::optional<GraphicItemRef> hit_test(float world_x, float world_y);

    std::size_t size() const { return scene_.size(); }
    std::uint64_t revision() const { return scene_.revision(); }
    termin::visual::VisualScene2D& visual_scene() { return scene_; }
    const termin::visual::VisualScene2D& visual_scene() const { return scene_; }
    Signal<GraphicsScene&>& changed() { return changed_; }

private:
    struct HandleKey {
        std::uint64_t scene_id = 0;
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
        friend bool operator==(const HandleKey&, const HandleKey&) = default;
    };
    struct HandleHash {
        std::size_t operator()(const HandleKey& value) const noexcept;
    };
    struct Metadata {
        std::string stable_id;
        bool selectable = true;
        bool draggable = false;
    };

    GraphicItemRef create_(
        std::string stable_id,
        termin::visual::GraphicItemPayload2D payload,
        const std::optional<GraphicItemRef>& parent);
    static HandleKey key_(termin::visual::GraphicItemHandle handle);
    std::optional<Metadata> metadata_(
        termin::visual::GraphicItemHandle handle) const;
    bool update_metadata_(
        termin::visual::GraphicItemHandle handle,
        const Metadata& metadata);
    void notify_changed_();

    termin::visual::VisualScene2D scene_;
    mutable std::mutex metadata_mutex_;
    std::unordered_map<HandleKey, Metadata, HandleHash> metadata_by_handle_;
    std::shared_ptr<void> lifetime_token_ = std::make_shared<int>(0);
    Signal<GraphicsScene&> changed_;

    friend class GraphicItemRef;
    friend class SceneView;
};

} // namespace termin::gui_native
