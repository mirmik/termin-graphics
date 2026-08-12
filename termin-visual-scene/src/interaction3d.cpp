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

    VisualItem3DHandle SceneInteraction3D::hit_handle_(const std::unordered_map<PointerId3D, HitResult3D>& values,
                                                       PointerId3D pointer) {
        const auto found = values.find(pointer);
        return found != values.end() ? found->second.item : tc_visual_item3d_handle_invalid();
    }

    std::optional<HitResult3D> SceneInteraction3D::hit_(
        const std::unordered_map<PointerId3D, HitResult3D>& values, PointerId3D pointer) {
        const auto found = values.find(pointer);
        return found != values.end() ? std::optional<HitResult3D>{found->second} : std::nullopt;
    }

    bool SceneInteraction3D::dispatch_target_(const TargetPointerEvent3D& event) {
        const auto handler = target_pointer_handlers_.find(key_(event.target));
        if (handler == target_pointer_handlers_.end())
            return false;
        try {
            handler->second(event);
        } catch (const std::exception& error) {
            tc::Log::error("SceneInteraction3D target pointer callback failed: %s", error.what());
            return true;
        } catch (...) {
            tc::Log::error("SceneInteraction3D target pointer callback failed with an unknown exception");
            return true;
        }
        return false;
    }

    void SceneInteraction3D::reconcile_(const TcVisualScene3D& scene,
                                        const PointerEvent3D& event,
                                        PointerDispatch3D& result) {
        const auto invalid = [&](VisualItem3DHandle handle) {
            const auto* item = scene.resolve(handle);
            return item == nullptr || !scene.effective_visible(*item) || !scene.effective_enabled(*item);
        };
        const auto hovered = hovered_.find(event.pointer);
        if (hovered != hovered_.end() && invalid(hovered->second.item)) {
            result.callback_failed |= dispatch_target_({TargetPointerEventKind3D::Leave,
                                                        event,
                                                        hovered->second.item,
                                                        hovered->second.part,
                                                        std::nullopt,
                                                        false});
            hovered_.erase(hovered);
        }
        const auto captured = captured_.find(event.pointer);
        if (captured != captured_.end() && invalid(captured->second.item)) {
            result.callback_failed |= dispatch_target_({TargetPointerEventKind3D::Cancel,
                                                        event,
                                                        captured->second.item,
                                                        captured->second.part,
                                                        std::nullopt,
                                                        true});
            captured_.erase(captured);
            pressed_.erase(event.pointer);
        } else {
            std::erase_if(pressed_, [&](const auto& pair) {
                return pair.first == event.pointer && invalid(pair.second.item);
            });
        }
        // Handlers intentionally survive until invalid targets have received
        // their final leave/cancel notification above.
        std::erase_if(target_pointer_handlers_, [&](const auto& pair) {
            return scene.resolve(VisualItem3DHandle{pair.first.scene_id, pair.first.index, pair.first.generation}) ==
                   nullptr;
        });
        std::erase_if(action_handlers_, [&](const auto& pair) {
            return scene.resolve(VisualItem3DHandle{pair.first.scene_id, pair.first.index, pair.first.generation}) ==
                   nullptr;
        });
    }

    PointerDispatch3D SceneInteraction3D::route(const TcVisualScene3D& scene, const PointerEvent3D& event) {
        PointerDispatch3D result;
        result.event = event;
        last_events_[event.pointer] = event;
        reconcile_(scene, event, result);
        if (event.kind != PointerEventKind3D::Cancel)
            result.hit = hit_test(scene, event.world_ray);
        const auto previous_hover = hit_(hovered_, event.pointer);
        const bool same_hover = event.kind != PointerEventKind3D::Cancel && previous_hover && result.hit &&
                                same_handle(previous_hover->item, result.hit->item) &&
                                previous_hover->part == result.hit->part;
        if (event.kind != PointerEventKind3D::Cancel && !same_hover) {
            if (previous_hover) {
                result.callback_failed |= dispatch_target_({TargetPointerEventKind3D::Leave,
                                                            event,
                                                            previous_hover->item,
                                                            previous_hover->part,
                                                            result.hit,
                                                            false});
            }
            if (result.hit) {
                result.callback_failed |= dispatch_target_({TargetPointerEventKind3D::Enter,
                                                            event,
                                                            result.hit->item,
                                                            result.hit->part,
                                                            result.hit,
                                                            false});
            }
        }
        // Cancel needs the previous hover below in order to deliver its final
        // Leave notification. Other event kinds publish the newly hit target.
        if (event.kind != PointerEventKind3D::Cancel) {
            if (result.hit)
                hovered_[event.pointer] = *result.hit;
            else
                hovered_.erase(event.pointer);
        }

        ActionHandler action_handler;
        if (event.kind == PointerEventKind3D::Down) {
            if (result.hit) {
                pressed_[event.pointer] = *result.hit;
                captured_[event.pointer] = *result.hit;
                result.target = result.hit->item;
                result.callback_failed |= dispatch_target_({TargetPointerEventKind3D::Down,
                                                            event,
                                                            result.hit->item,
                                                            result.hit->part,
                                                            result.hit,
                                                            true});
            }
        } else if (event.kind == PointerEventKind3D::Move) {
            const auto capture = hit_(captured_, event.pointer);
            result.target = capture ? capture->item : tc_visual_item3d_handle_invalid();
            if (!valid_handle(result.target) && result.hit)
                result.target = result.hit->item;
            const auto target_hit = capture ? capture : result.hit;
            if (target_hit) {
                result.callback_failed |= dispatch_target_({TargetPointerEventKind3D::Move,
                                                            event,
                                                            target_hit->item,
                                                            target_hit->part,
                                                            result.hit,
                                                            capture.has_value()});
            }
        } else if (event.kind == PointerEventKind3D::Up) {
            const auto capture = hit_(captured_, event.pointer);
            result.target = capture ? capture->item : tc_visual_item3d_handle_invalid();
            if (!valid_handle(result.target) && result.hit)
                result.target = result.hit->item;
            const auto target_hit = capture ? capture : result.hit;
            if (target_hit) {
                result.callback_failed |= dispatch_target_({TargetPointerEventKind3D::Up,
                                                            event,
                                                            target_hit->item,
                                                            target_hit->part,
                                                            result.hit,
                                                            capture.has_value()});
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
            const auto capture = hit_(captured_, event.pointer);
            result.target = capture ? capture->item : tc_visual_item3d_handle_invalid();
            if (!valid_handle(result.target)) {
                result.target = hit_handle_(pressed_, event.pointer);
            }
            const auto target_hit = capture ? capture : hit_(pressed_, event.pointer);
            if (target_hit) {
                result.callback_failed |= dispatch_target_({TargetPointerEventKind3D::Cancel,
                                                            event,
                                                            target_hit->item,
                                                            target_hit->part,
                                                            result.hit,
                                                            capture.has_value()});
            }
            const auto hover = hit_(hovered_, event.pointer);
            if (hover) {
                result.callback_failed |= dispatch_target_({TargetPointerEventKind3D::Leave,
                                                            event,
                                                            hover->item,
                                                            hover->part,
                                                            std::nullopt,
                                                            false});
            }
            hovered_.erase(event.pointer);
            pressed_.erase(event.pointer);
            captured_.erase(event.pointer);
        }

        result.hovered = hit_handle_(hovered_, event.pointer);
        result.pressed = hit_handle_(pressed_, event.pointer);
        result.captured = hit_handle_(captured_, event.pointer);
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

    bool SceneInteraction3D::capture(const TcVisualScene3D& scene,
                                     PointerId3D pointer,
                                     VisualItem3DHandle target,
                                     std::uint64_t part) {
        const auto* item = scene.resolve(target);
        if (item == nullptr || !scene.effective_visible(*item) || !scene.effective_enabled(*item)) {
            return false;
        }
        captured_[pointer] = HitResult3D{target, 0.0, part, {}, {}};
        return true;
    }

    void SceneInteraction3D::release(PointerId3D pointer) {
        captured_.erase(pointer);
    }

    void SceneInteraction3D::cancel_all() {
        hovered_.clear();
        pressed_.clear();
        captured_.clear();
        last_events_.clear();
    }

    bool SceneInteraction3D::cancel_all(const TcVisualScene3D& scene) {
        bool callback_failed = false;
        const auto events = last_events_;
        FallbackHandler fallback = std::move(fallback_handler_);
        fallback_handler_ = {};
        for (const auto& [pointer, previous] : events) {
            if (!hovered_.contains(pointer) && !pressed_.contains(pointer) && !captured_.contains(pointer))
                continue;
            PointerEvent3D cancel = previous;
            cancel.kind = PointerEventKind3D::Cancel;
            callback_failed |= route(scene, cancel).callback_failed;
        }
        fallback_handler_ = std::move(fallback);
        cancel_all();
        return callback_failed;
    }

    void SceneInteraction3D::set_target_pointer_handler(VisualItem3DHandle item, TargetPointerHandler handler) {
        if (handler)
            target_pointer_handlers_[key_(item)] = std::move(handler);
        else
            target_pointer_handlers_.erase(key_(item));
    }

    void SceneInteraction3D::clear_target_pointer_handler(VisualItem3DHandle item) {
        target_pointer_handlers_.erase(key_(item));
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
        return hit_handle_(captured_, pointer);
    }

    std::optional<HitResult3D> SceneInteraction3D::hovered_hit(PointerId3D pointer) const {
        return hit_(hovered_, pointer);
    }

    std::optional<HitResult3D> SceneInteraction3D::pressed_hit(PointerId3D pointer) const {
        return hit_(pressed_, pointer);
    }

    std::optional<HitResult3D> SceneInteraction3D::captured_hit(PointerId3D pointer) const {
        return hit_(captured_, pointer);
    }

} // namespace termin::visual
