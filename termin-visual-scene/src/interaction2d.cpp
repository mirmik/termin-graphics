#include "termin_visual_scene/interaction2d.hpp"

#include "scene_composition2d_internal.hpp"

#include <algorithm>
#include <cmath>

#include <tcbase/tc_log.hpp>

namespace termin::visual {
    namespace {

        bool equal_handle(GraphicItemHandle left, GraphicItemHandle right) {
            return left.scene_id == right.scene_id && left.index == right.index && left.generation == right.generation;
        }

        bool valid_handle(GraphicItemHandle value) {
            return !tc_graphic_item_handle_is_invalid(value);
        }

    } // namespace

    bool TcVisualScene::visit_hit_layers(termin::Vec2f world_point, const SceneHitLayerVisitor2D& visitor) const {
        if (!std::isfinite(world_point.x) || !std::isfinite(world_point.y)) {
            tc::Log::error("TcVisualScene hit_test rejected non-finite point");
            return false;
        }
        if (!visitor) {
            tc::Log::error("TcVisualScene hit-layer traversal requires a visitor");
            return false;
        }

        tgfx::CompositionEvaluator2D composition;
        composition.begin_batch();
        std::function<bool(tc_graphic_item&, bool)> visit;
        visit = [&](tc_graphic_item& item, bool parent_enabled) -> bool {
            const bool enabled = parent_enabled && item.enabled;
            tgfx::CompositionLayer2D layer;
            if (!detail::composition_layer(item, layer) || !composition.push(layer)) {
                return false;
            }

            bool stopped = false;
            if (enabled && composition.drawable() && composition.clips_contain(world_point)) {
                if (!composition.state().invertible) {
                    tc::Log::error("graphic item '%s' has a singular world transform and is not hittable",
                                   tc_graphic_item_type_name(&item));
                } else {
                    auto children = sorted_children_(&item);
                    for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
                        stopped = visit(**iterator, enabled);
                        if (stopped) {
                            break;
                        }
                    }

                    if (!stopped) {
                        termin::Vec2f local{};
                        if (composition.map_point_from_world(world_point, local)) {
                            const bool item_hit = item.vtable != nullptr && item.vtable->hit_test != nullptr &&
                                                  item.vtable->hit_test(&item, local, 0.0f);
                            stopped = visitor(item, local, item_hit);
                        }
                    }
                }
            }
            if (!composition.active() || !composition.pop()) {
                return false;
            }
            return stopped;
        };

        auto roots = sorted_roots_();
        for (auto iterator = roots.rbegin(); iterator != roots.rend(); ++iterator) {
            if (visit(**iterator, true)) {
                if (!composition.end_batch()) {
                    return false;
                }
                return true;
            }
            if (!composition.active()) {
                return false;
            }
        }
        if (!composition.end_batch()) {
            return false;
        }
        return false;
    }

    std::optional<GraphicItemHandle> hit_test(const TcVisualScene& scene, termin::Vec2f world_point) {
        std::optional<GraphicItemHandle> result;
        scene.visit_hit_layers(world_point, [&](const tc_graphic_item& item, termin::Vec2f, bool item_hit) {
            if (!item_hit) {
                return false;
            }
            result = item.handle;
            return true;
        });
        return result;
    }

    std::size_t SceneInteraction2D::HandleHash::operator()(const HandleKey& value) const noexcept {
        std::size_t result = std::hash<std::uint64_t>{}(value.scene_id);
        result ^= std::hash<std::uint32_t>{}(value.index) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
        result ^= std::hash<std::uint32_t>{}(value.generation) + 0x9e3779b9u + (result << 6u) + (result >> 2u);
        return result;
    }

    SceneInteraction2D::HandleKey SceneInteraction2D::key_(GraphicItemHandle handle) {
        return {
            handle.scene_id,
            handle.index,
            handle.generation,
        };
    }

    GraphicItemHandle SceneInteraction2D::handle_(const std::unordered_map<PointerId2D, GraphicItemHandle>& values,
                                                  PointerId2D pointer) {
        const auto found = values.find(pointer);
        return found != values.end() ? found->second : tc_graphic_item_handle_invalid();
    }

    void SceneInteraction2D::reconcile_(const TcVisualScene& scene) {
        const auto reconcile = [&](auto& values) {
            std::erase_if(values, [&](const auto& pair) {
                const auto* item = scene.resolve(pair.second);
                return item == nullptr || !scene.effective_enabled(*item);
            });
        };
        reconcile(hovered_);
        reconcile(pressed_);
        reconcile(captured_);
        std::erase_if(action_handlers_, [&](const auto& pair) {
            const GraphicItemHandle handle{
                pair.first.scene_id,
                pair.first.index,
                pair.first.generation,
            };
            return scene.resolve(handle) == nullptr;
        });
    }

    PointerDispatch2D SceneInteraction2D::route(const TcVisualScene& scene, const PointerEvent2D& event) {
        reconcile_(scene);
        PointerDispatch2D result;
        result.event = event;
        const auto hit = hit_test(scene, event.position);
        result.hit_target = hit.value_or(tc_graphic_item_handle_invalid());
        if (hit) {
            hovered_[event.pointer] = *hit;
        } else {
            hovered_.erase(event.pointer);
        }

        ActionHandler action_handler;
        if (event.kind == PointerEventKind2D::Down) {
            if (hit) {
                pressed_[event.pointer] = *hit;
                captured_[event.pointer] = *hit;
                result.target = *hit;
            }
        } else if (event.kind == PointerEventKind2D::Move) {
            result.target = handle_(captured_, event.pointer);
            if (!valid_handle(result.target)) {
                result.target = result.hit_target;
            }
        } else if (event.kind == PointerEventKind2D::Up) {
            result.target = handle_(captured_, event.pointer);
            if (!valid_handle(result.target)) {
                result.target = result.hit_target;
            }
            const auto pressed = handle_(pressed_, event.pointer);
            if (valid_handle(pressed) && hit && equal_handle(pressed, *hit)) {
                result.action = ActionEvent2D{pressed, event.pointer, "activate"};
                const auto handler = action_handlers_.find(key_(pressed));
                if (handler != action_handlers_.end()) {
                    action_handler = handler->second;
                }
            }
            pressed_.erase(event.pointer);
            captured_.erase(event.pointer);
        } else {
            result.target = handle_(captured_, event.pointer);
            if (!valid_handle(result.target)) {
                result.target = handle_(pressed_, event.pointer);
            }
            hovered_.erase(event.pointer);
            pressed_.erase(event.pointer);
            captured_.erase(event.pointer);
        }

        result.hovered = handle_(hovered_, event.pointer);
        result.pressed = handle_(pressed_, event.pointer);
        result.captured = handle_(captured_, event.pointer);
        result.used_fallback = !valid_handle(result.target);
        if (result.action && action_handler) {
            action_handler(*result.action);
        }
        if (result.used_fallback && fallback_handler_) {
            fallback_handler_(event);
        }
        return result;
    }

    bool SceneInteraction2D::capture(const TcVisualScene& scene, PointerId2D pointer, GraphicItemHandle target) {
        const auto* item = scene.resolve(target);
        if (item == nullptr || !scene.effective_enabled(*item)) {
            return false;
        }
        captured_[pointer] = target;
        return true;
    }

    void SceneInteraction2D::release(PointerId2D pointer) {
        captured_.erase(pointer);
    }

    void SceneInteraction2D::cancel_all() {
        hovered_.clear();
        pressed_.clear();
        captured_.clear();
    }

    void SceneInteraction2D::set_action_handler(GraphicItemHandle item, ActionHandler handler) {
        if (handler) {
            action_handlers_[key_(item)] = std::move(handler);
        } else {
            action_handlers_.erase(key_(item));
        }
    }

    void SceneInteraction2D::clear_action_handler(GraphicItemHandle item) {
        action_handlers_.erase(key_(item));
    }

    void SceneInteraction2D::set_fallback_handler(FallbackHandler handler) {
        fallback_handler_ = std::move(handler);
    }

    GraphicItemHandle SceneInteraction2D::hovered(PointerId2D pointer) const {
        return handle_(hovered_, pointer);
    }

    GraphicItemHandle SceneInteraction2D::pressed(PointerId2D pointer) const {
        return handle_(pressed_, pointer);
    }

    GraphicItemHandle SceneInteraction2D::captured(PointerId2D pointer) const {
        return handle_(captured_, pointer);
    }

    void SelectionController2D::handle(const TcVisualScene& scene, const PointerDispatch2D& dispatch) {
        if (dispatch.event.kind != PointerEventKind2D::Down) {
            return;
        }
        selection_.clear();
        if (valid_handle(dispatch.target)) {
            select(scene, dispatch.target);
        }
    }

    void SelectionController2D::clear() {
        selection_.clear();
    }

    bool SelectionController2D::select(const TcVisualScene& scene, GraphicItemHandle item) {
        selection_.clear();
        if (!valid_handle(item))
            return true;
        const auto* object = scene.resolve(item);
        if (object == nullptr || !scene.effective_enabled(*object)) {
            return false;
        }
        selection_.push_back(item);
        return true;
    }

    bool SelectionController2D::toggle(const TcVisualScene& scene, GraphicItemHandle item) {
        reconcile(scene);
        if (!valid_handle(item))
            return true;
        const auto found = std::find_if(
            selection_.begin(), selection_.end(), [&](const auto current) { return equal_handle(current, item); });
        if (found != selection_.end()) {
            selection_.erase(found);
            return true;
        }
        const auto* object = scene.resolve(item);
        if (object == nullptr || !scene.effective_enabled(*object)) {
            return false;
        }
        selection_.push_back(item);
        return true;
    }

    void SelectionController2D::reconcile(const TcVisualScene& scene) {
        std::erase_if(selection_, [&](const auto handle) {
            const auto* item = scene.resolve(handle);
            return item == nullptr || !scene.effective_enabled(*item);
        });
    }

    bool DragController2D::handle(TcVisualScene& scene, const PointerDispatch2D& dispatch) {
        if (dispatch.event.kind == PointerEventKind2D::Down && valid_handle(dispatch.target)) {
            auto* item = scene.resolve(dispatch.target);
            if (item == nullptr)
                return false;
            auto parent_world = termin::Affine2f::identity();
            if (item->parent != nullptr) {
                parent_world = scene.world_transform(*item->parent);
            }
            if (!parent_world.try_inverse(world_to_parent_)) {
                return false;
            }
            target_ = dispatch.target;
            pointer_ = dispatch.event.pointer;
            start_world_ = dispatch.event.position;
            start_transform_ = item->local_transform;
            return true;
        }
        if (!valid_handle(target_) || dispatch.event.pointer != pointer_) {
            return false;
        }
        if (dispatch.event.kind == PointerEventKind2D::Move) {
            if (!equal_handle(dispatch.target, target_)) {
                cancel();
                return false;
            }
            auto* item = scene.resolve(target_);
            if (item == nullptr) {
                cancel();
                return false;
            }
            const termin::Vec2f world_delta{
                dispatch.event.position.x - start_world_.x,
                dispatch.event.position.y - start_world_.y,
            };
            const auto parent_delta = world_to_parent_.transform_vector(world_delta);
            item->local_transform = termin::Affine2f::translation(parent_delta) * start_transform_;
            return true;
        }
        if (dispatch.event.kind == PointerEventKind2D::Up || dispatch.event.kind == PointerEventKind2D::Cancel) {
            cancel();
            return true;
        }
        return false;
    }

    void DragController2D::cancel() noexcept {
        target_ = tc_graphic_item_handle_invalid();
        pointer_ = 0;
    }

} // namespace termin::visual
