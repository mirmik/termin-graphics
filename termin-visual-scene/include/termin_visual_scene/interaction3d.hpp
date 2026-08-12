#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

#include "termin_visual_scene/export.h"
#include "termin_visual_scene/scene3d.hpp"

namespace termin::visual {

    using PointerId3D = std::uint64_t;

    enum class PointerEventKind3D : std::uint8_t {
        Move,
        Down,
        Up,
        Cancel,
    };

    // Projection is a host concern: route() receives a ready world ray, not a
    // viewport coordinate or camera.
    struct PointerEvent3D {
        PointerId3D pointer = 0;
        PointerEventKind3D kind = PointerEventKind3D::Move;
        termin::Ray3 world_ray{};
        std::uint32_t button = 0;
    };

    struct ActionEvent3D {
        VisualItem3DHandle target = tc_visual_item3d_handle_invalid();
        PointerId3D pointer = 0;
        std::uint64_t part = 0;
        std::string action = "activate";
    };

    enum class TargetPointerEventKind3D : std::uint8_t {
        Enter,
        Leave,
        Down,
        Move,
        Up,
        Cancel,
    };

    // Delivered to an external controller registered for one item. The visual
    // item and scene remain presentation-only; drag/edit behavior belongs to
    // the handler owner. During capture, part is the original pressed part
    // even when current_hit is empty or points at another item.
    struct TargetPointerEvent3D {
        TargetPointerEventKind3D kind = TargetPointerEventKind3D::Move;
        PointerEvent3D pointer_event{};
        VisualItem3DHandle target = tc_visual_item3d_handle_invalid();
        std::uint64_t part = 0;
        std::optional<HitResult3D> current_hit;
        bool captured = false;
    };

    struct PointerDispatch3D {
        PointerEvent3D event;
        VisualItem3DHandle target = tc_visual_item3d_handle_invalid();
        std::optional<HitResult3D> hit;
        VisualItem3DHandle hovered = tc_visual_item3d_handle_invalid();
        VisualItem3DHandle pressed = tc_visual_item3d_handle_invalid();
        VisualItem3DHandle captured = tc_visual_item3d_handle_invalid();
        bool used_fallback = false;
        bool callback_failed = false;
        std::optional<ActionEvent3D> action;
    };

    class TERMIN_VISUAL_SCENE_API SceneInteraction3D {
    public:
        using ActionHandler = std::function<void(const ActionEvent3D&)>;
        using TargetPointerHandler = std::function<void(const TargetPointerEvent3D&)>;
        using FallbackHandler = std::function<void(const PointerEvent3D&)>;

        PointerDispatch3D route(const TcVisualScene3D& scene, const PointerEvent3D& event);
        bool capture(const TcVisualScene3D& scene,
                     PointerId3D pointer,
                     VisualItem3DHandle target,
                     std::uint64_t part = 0);
        void release(PointerId3D pointer);
        void cancel_all();
        bool cancel_all(const TcVisualScene3D& scene);

        void set_target_pointer_handler(VisualItem3DHandle item, TargetPointerHandler handler);
        void clear_target_pointer_handler(VisualItem3DHandle item);
        void set_action_handler(VisualItem3DHandle item, ActionHandler handler);
        void clear_action_handler(VisualItem3DHandle item);
        void set_fallback_handler(FallbackHandler handler);

        VisualItem3DHandle hovered(PointerId3D pointer) const;
        VisualItem3DHandle pressed(PointerId3D pointer) const;
        VisualItem3DHandle captured(PointerId3D pointer) const;
        std::optional<HitResult3D> hovered_hit(PointerId3D pointer) const;
        std::optional<HitResult3D> pressed_hit(PointerId3D pointer) const;
        std::optional<HitResult3D> captured_hit(PointerId3D pointer) const;

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

        static HandleKey key_(VisualItem3DHandle handle);
        static VisualItem3DHandle hit_handle_(const std::unordered_map<PointerId3D, HitResult3D>& values,
                                              PointerId3D pointer);
        static std::optional<HitResult3D> hit_(const std::unordered_map<PointerId3D, HitResult3D>& values,
                                              PointerId3D pointer);
        bool dispatch_target_(const TargetPointerEvent3D& event);
        void reconcile_(const TcVisualScene3D& scene, const PointerEvent3D& event, PointerDispatch3D& result);

        std::unordered_map<PointerId3D, HitResult3D> hovered_;
        std::unordered_map<PointerId3D, HitResult3D> pressed_;
        std::unordered_map<PointerId3D, HitResult3D> captured_;
        std::unordered_map<PointerId3D, PointerEvent3D> last_events_;
        std::unordered_map<HandleKey, TargetPointerHandler, HandleHash> target_pointer_handlers_;
        std::unordered_map<HandleKey, ActionHandler, HandleHash> action_handlers_;
        FallbackHandler fallback_handler_;
    };

} // namespace termin::visual
