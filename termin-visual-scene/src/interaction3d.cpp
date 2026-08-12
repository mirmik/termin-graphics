#include "termin_visual_scene/interaction3d.hpp"

#include <exception>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        bool valid_handle(VisualItem3DHandle value) {
            return !tc_visual_item3d_handle_is_invalid(value);
        }

        bool same_handle(VisualItem3DHandle left, VisualItem3DHandle right) {
            return tc_visual_item3d_handle_eq(left, right);
        }

    } // namespace

    std::size_t SceneInteraction3D::HandleHash::operator()(const HandleKey& value) const noexcept {
        std::size_t result = std::hash<std::uint64_t>{}(value.scene_id);
        result ^= std::hash<std::uint32_t>{}(value.index) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
        result ^= std::hash<std::uint32_t>{}(value.generation) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
        return result;
    }

    SceneInteraction3D::HandleKey SceneInteraction3D::key_(VisualItem3DHandle handle) {
        return {handle.scene_id, handle.index, handle.generation};
    }

    VisualItem3DHandle SceneInteraction3D::handle_(const std::unordered_map<PointerId3D, VisualItem3DHandle>& values,
                                                   PointerId3D pointer) {
        const auto found = values.find(pointer);
        return found != values.end() ? found->second : tc_visual_item3d_handle_invalid();
    }

    VisualItem3DHandle SceneInteraction3D::hit_handle_(const std::unordered_map<PointerId3D, HitResult3D>& values,
                                                       PointerId3D pointer) {
        const auto found = values.find(pointer);
        return found != values.end() ? found->second.item : tc_visual_item3d_handle_invalid();
    }

    void SceneInteraction3D::reconcile_(const TcVisualScene3D& scene) {
        const auto invalid = [&](VisualItem3DHandle handle) {
            const auto* item = scene.resolve(handle);
            return item == nullptr || !scene.effective_visible(*item) || !scene.effective_enabled(*item);
        };
        std::erase_if(hovered_, [&](const auto& pair) { return invalid(pair.second.item); });
        std::erase_if(pressed_, [&](const auto& pair) { return invalid(pair.second.item); });
        std::erase_if(captured_, [&](const auto& pair) { return invalid(pair.second); });
        std::erase_if(action_handlers_, [&](const auto& pair) {
            return scene.resolve(VisualItem3DHandle{pair.first.scene_id, pair.first.index, pair.first.generation}) ==
                   nullptr;
        });
    }

    PointerDispatch3D SceneInteraction3D::route(const TcVisualScene3D& scene, const PointerEvent3D& event) {
        reconcile_(scene);
        PointerDispatch3D result;
        result.event = event;
        result.hit = hit_test(scene, event.world_ray);
        if (result.hit) {
            hovered_[event.pointer] = *result.hit;
        } else {
            hovered_.erase(event.pointer);
        }

        ActionHandler action_handler;
        if (event.kind == PointerEventKind3D::Down) {
            if (result.hit) {
                pressed_[event.pointer] = *result.hit;
                captured_[event.pointer] = result.hit->item;
                result.target = result.hit->item;
            }
        } else if (event.kind == PointerEventKind3D::Move) {
            result.target = handle_(captured_, event.pointer);
            if (!valid_handle(result.target) && result.hit) {
                result.target = result.hit->item;
            }
        } else if (event.kind == PointerEventKind3D::Up) {
            result.target = handle_(captured_, event.pointer);
            if (!valid_handle(result.target) && result.hit) {
                result.target = result.hit->item;
            }
            const auto pressed = pressed_.find(event.pointer);
            if (pressed != pressed_.end() && result.hit && same_handle(pressed->second.item, result.hit->item) &&
                pressed->second.part == result.hit->part) {
                result.action = ActionEvent3D{pressed->second.item, event.pointer, pressed->second.part, "activate"};
                const auto handler = action_handlers_.find(key_(pressed->second.item));
                if (handler != action_handlers_.end()) {
                    action_handler = handler->second;
                }
            }
            pressed_.erase(event.pointer);
            captured_.erase(event.pointer);
        } else {
            result.target = handle_(captured_, event.pointer);
            if (!valid_handle(result.target)) {
                result.target = hit_handle_(pressed_, event.pointer);
            }
            hovered_.erase(event.pointer);
            pressed_.erase(event.pointer);
            captured_.erase(event.pointer);
        }

        result.hovered = hit_handle_(hovered_, event.pointer);
        result.pressed = hit_handle_(pressed_, event.pointer);
        result.captured = handle_(captured_, event.pointer);
        result.used_fallback = !valid_handle(result.target);
        if (result.action && action_handler) {
            try {
                action_handler(*result.action);
            } catch (const std::exception& error) {
                tc::Log::error("SceneInteraction3D action callback failed: %s", error.what());
                result.callback_failed = true;
            } catch (...) {
                tc::Log::error("SceneInteraction3D action callback failed with an unknown exception");
                result.callback_failed = true;
            }
        }
        if (result.used_fallback && fallback_handler_) {
            try {
                fallback_handler_(event);
            } catch (const std::exception& error) {
                tc::Log::error("SceneInteraction3D fallback callback failed: %s", error.what());
                result.callback_failed = true;
            } catch (...) {
                tc::Log::error("SceneInteraction3D fallback callback failed with an unknown exception");
                result.callback_failed = true;
            }
        }
        return result;
    }

    bool SceneInteraction3D::capture(const TcVisualScene3D& scene, PointerId3D pointer, VisualItem3DHandle target) {
        const auto* item = scene.resolve(target);
        if (item == nullptr || !scene.effective_visible(*item) || !scene.effective_enabled(*item)) {
            return false;
        }
        captured_[pointer] = target;
        return true;
    }

    void SceneInteraction3D::release(PointerId3D pointer) {
        captured_.erase(pointer);
    }

    void SceneInteraction3D::cancel_all() {
        hovered_.clear();
        pressed_.clear();
        captured_.clear();
    }

    void SceneInteraction3D::set_action_handler(VisualItem3DHandle item, ActionHandler handler) {
        if (handler) {
            action_handlers_[key_(item)] = std::move(handler);
        } else {
            action_handlers_.erase(key_(item));
        }
    }

    void SceneInteraction3D::clear_action_handler(VisualItem3DHandle item) {
        action_handlers_.erase(key_(item));
    }

    void SceneInteraction3D::set_fallback_handler(FallbackHandler handler) {
        fallback_handler_ = std::move(handler);
    }

    VisualItem3DHandle SceneInteraction3D::hovered(PointerId3D pointer) const {
        return hit_handle_(hovered_, pointer);
    }

    VisualItem3DHandle SceneInteraction3D::pressed(PointerId3D pointer) const {
        return hit_handle_(pressed_, pointer);
    }

    VisualItem3DHandle SceneInteraction3D::captured(PointerId3D pointer) const {
        return handle_(captured_, pointer);
    }

} // namespace termin::visual
