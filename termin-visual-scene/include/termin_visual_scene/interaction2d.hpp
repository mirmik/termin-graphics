#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/scene2d.hpp"

namespace termin::visual {

using PointerId2D = std::uint64_t;

enum class PointerEventKind2D : std::uint8_t {
    Move,
    Down,
    Up,
    Cancel,
};

struct PointerEvent2D {
    PointerId2D pointer = 0;
    PointerEventKind2D kind = PointerEventKind2D::Move;
    termin::Vec2f position{};
    std::uint32_t button = 0;
};

struct ActionEvent2D {
    GraphicItemHandle target = tc_graphic_item_handle_invalid();
    PointerId2D pointer = 0;
    std::string action = "activate";
};

struct PointerDispatch2D {
    PointerEvent2D event;
    GraphicItemHandle target = tc_graphic_item_handle_invalid();
    GraphicItemHandle hit_target = tc_graphic_item_handle_invalid();
    GraphicItemHandle hovered = tc_graphic_item_handle_invalid();
    GraphicItemHandle pressed = tc_graphic_item_handle_invalid();
    GraphicItemHandle captured = tc_graphic_item_handle_invalid();
    bool used_fallback = false;
    std::optional<ActionEvent2D> action;
};

TERMIN_VISUAL_SCENE_API std::optional<GraphicItemHandle> hit_test(
    const VisualScene2D& scene,
    termin::Vec2f world_point);

class TERMIN_VISUAL_SCENE_API SceneInteraction2D {
public:
    using ActionHandler = std::function<void(const ActionEvent2D&)>;
    using FallbackHandler = std::function<void(const PointerEvent2D&)>;

    PointerDispatch2D route(
        const VisualScene2D& scene,
        const PointerEvent2D& event);

    bool capture(
        const VisualScene2D& scene,
        PointerId2D pointer,
        GraphicItemHandle target);
    void release(PointerId2D pointer);
    void cancel_all();

    void set_action_handler(GraphicItemHandle item, ActionHandler handler);
    void clear_action_handler(GraphicItemHandle item);
    void set_fallback_handler(FallbackHandler handler);

    GraphicItemHandle hovered(PointerId2D pointer) const;
    GraphicItemHandle pressed(PointerId2D pointer) const;
    GraphicItemHandle captured(PointerId2D pointer) const;

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
    struct Tracked {
        GraphicItemHandle handle = tc_graphic_item_handle_invalid();
        std::uint64_t topology_revision = 0;
    };

    static HandleKey key_(GraphicItemHandle handle);
    static GraphicItemHandle handle_(
        const std::unordered_map<PointerId2D, Tracked>& values,
        PointerId2D pointer);
    void reconcile_locked_(
        const std::vector<GraphicItemSnapshot2D>& snapshots);

    mutable std::mutex mutex_;
    std::unordered_map<PointerId2D, Tracked> hovered_;
    std::unordered_map<PointerId2D, Tracked> pressed_;
    std::unordered_map<PointerId2D, Tracked> captured_;
    std::unordered_map<HandleKey, ActionHandler, HandleHash> action_handlers_;
    FallbackHandler fallback_handler_;
};

class TERMIN_VISUAL_SCENE_API SelectionController2D {
public:
    void handle(
        const VisualScene2D& scene,
        const PointerDispatch2D& dispatch);
    void clear();
    bool select(const VisualScene2D& scene, GraphicItemHandle item);
    bool toggle(const VisualScene2D& scene, GraphicItemHandle item);
    void reconcile(const VisualScene2D& scene);
    std::vector<GraphicItemHandle> selection() const;

private:
    struct Entry {
        GraphicItemHandle handle = tc_graphic_item_handle_invalid();
        std::uint64_t topology_revision = 0;
    };
    std::vector<Entry> selection_;
};

class TERMIN_VISUAL_SCENE_API DragController2D {
public:
    bool handle(VisualScene2D& scene, const PointerDispatch2D& dispatch);
    void cancel() noexcept;
    GraphicItemHandle target() const noexcept { return target_; }

private:
    GraphicItemHandle target_ = tc_graphic_item_handle_invalid();
    PointerId2D pointer_ = 0;
    termin::Vec2f start_world_{};
    GraphicItemState2D start_state_;
    termin::Affine2f world_to_parent_ = termin::Affine2f::identity();
};

}  // namespace termin::visual
