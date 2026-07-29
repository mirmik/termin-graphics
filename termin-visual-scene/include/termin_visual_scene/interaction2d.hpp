#pragma once

#include <cstdint>
#include <functional>
#include <optional>
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
    GraphicItemHandle target =
        tc_graphic_item_handle_invalid();
    PointerId2D pointer = 0;
    std::string action = "activate";
};

struct PointerDispatch2D {
    PointerEvent2D event;
    GraphicItemHandle target =
        tc_graphic_item_handle_invalid();
    GraphicItemHandle hit_target =
        tc_graphic_item_handle_invalid();
    GraphicItemHandle hovered =
        tc_graphic_item_handle_invalid();
    GraphicItemHandle pressed =
        tc_graphic_item_handle_invalid();
    GraphicItemHandle captured =
        tc_graphic_item_handle_invalid();
    bool used_fallback = false;
    std::optional<ActionEvent2D> action;
};

class TERMIN_VISUAL_SCENE_API SceneInteraction2D {
public:
    using ActionHandler =
        std::function<void(const ActionEvent2D&)>;
    using FallbackHandler =
        std::function<void(const PointerEvent2D&)>;

    PointerDispatch2D route(
        const TcVisualScene& scene,
        const PointerEvent2D& event);
    bool capture(
        const TcVisualScene& scene,
        PointerId2D pointer,
        GraphicItemHandle target);
    void release(PointerId2D pointer);
    void cancel_all();

    void set_action_handler(
        GraphicItemHandle item,
        ActionHandler handler);
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
        friend bool operator==(
            const HandleKey&,
            const HandleKey&) = default;
    };
    struct HandleHash {
        std::size_t operator()(
            const HandleKey& value) const noexcept;
    };

    static HandleKey key_(GraphicItemHandle handle);
    static GraphicItemHandle handle_(
        const std::unordered_map<
            PointerId2D,
            GraphicItemHandle>& values,
        PointerId2D pointer);
    void reconcile_(const TcVisualScene& scene);

    std::unordered_map<PointerId2D, GraphicItemHandle>
        hovered_;
    std::unordered_map<PointerId2D, GraphicItemHandle>
        pressed_;
    std::unordered_map<PointerId2D, GraphicItemHandle>
        captured_;
    std::unordered_map<
        HandleKey,
        ActionHandler,
        HandleHash> action_handlers_;
    FallbackHandler fallback_handler_;
};

class TERMIN_VISUAL_SCENE_API SelectionController2D {
public:
    void handle(
        const TcVisualScene& scene,
        const PointerDispatch2D& dispatch);
    void clear();
    bool select(
        const TcVisualScene& scene,
        GraphicItemHandle item);
    bool toggle(
        const TcVisualScene& scene,
        GraphicItemHandle item);
    void reconcile(const TcVisualScene& scene);
    const std::vector<GraphicItemHandle>& selection()
        const noexcept {
        return selection_;
    }

private:
    std::vector<GraphicItemHandle> selection_;
};

class TERMIN_VISUAL_SCENE_API DragController2D {
public:
    bool handle(
        TcVisualScene& scene,
        const PointerDispatch2D& dispatch);
    void cancel() noexcept;
    GraphicItemHandle target() const noexcept {
        return target_;
    }

private:
    GraphicItemHandle target_ =
        tc_graphic_item_handle_invalid();
    PointerId2D pointer_ = 0;
    termin::Vec2f start_world_{};
    termin::Affine2f start_transform_ =
        termin::Affine2f::identity();
    termin::Affine2f world_to_parent_ =
        termin::Affine2f::identity();
};

}  // namespace termin::visual
