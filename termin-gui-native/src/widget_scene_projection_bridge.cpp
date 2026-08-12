#include <termin/gui_native/widget_scene_projection_bridge.hpp>

#include "widgets_internal.hpp"

#include <algorithm>
#include <cmath>

#include <tcbase/tc_log.h>

namespace termin::gui_native {
    namespace {

        bool same_source(tc_graphic_item_handle left, tc_graphic_item_handle right) {
            return left.scene_id == right.scene_id && left.index == right.index && left.generation == right.generation;
        }

        bool finite_bounds(tc_bounds2f bounds) {
            return std::isfinite(bounds.x0) && std::isfinite(bounds.y0) && std::isfinite(bounds.x1) &&
                   std::isfinite(bounds.y1) && bounds.x0 <= bounds.x1 && bounds.y0 <= bounds.y1;
        }

        bool uniform_placement(tc_affine2f affine, tc_ui_uniform_transform& out) {
            if (!tc_affine2f_is_finite(affine))
                return false;
            const float scale = std::max({1.0f, std::fabs(affine.m00), std::fabs(affine.m11)});
            const float epsilon = 1.0e-5f * scale;
            if (affine.m00 <= 0.0f || std::fabs(affine.m01) > epsilon || std::fabs(affine.m10) > epsilon ||
                std::fabs(affine.m00 - affine.m11) > epsilon) {
                return false;
            }
            out = {{affine.tx, affine.ty}, affine.m00};
            return tc_ui_uniform_transform_is_valid(&out);
        }

    } // namespace

    WidgetSceneProjectionBridge::WidgetSceneProjectionBridge(SourceResolver resolver)
        : resolver_(std::move(resolver)) {}

    WidgetSceneProjectionBridge::~WidgetSceneProjectionBridge() {
        detach_all();
    }

    void WidgetSceneProjectionBridge::set_source_resolver(SourceResolver resolver) {
        resolver_ = std::move(resolver);
        reconcile();
    }

    void WidgetSceneProjectionBridge::bind_target(tc_ui_document_handle document, tc_widget_handle host) {
        if (!tc_ui_document_handle_eq(document_, document) || !tc_widget_handle_eq(host_, host)) {
            detach_all();
            document_ = document;
            host_ = host;
        }
    }

    void WidgetSceneProjectionBridge::set_camera(tc_affine2f world_to_parent) {
        if (!tc_affine2f_is_finite(world_to_parent)) {
            tc_log_error("[termin-gui-native] projection bridge rejected a non-finite camera");
            return;
        }
        camera_ = world_to_parent;
        layout();
    }

    void WidgetSceneProjectionBridge::set_policy(WidgetSceneProjectionPolicy policy) {
        policy_ = policy;
    }

    WidgetSceneProjectionPolicy WidgetSceneProjectionBridge::policy() const {
        return policy_;
    }

    bool WidgetSceneProjectionBridge::set_projection(tc_graphic_item_handle source, tc_widget_handle target) {
        if (tc_graphic_item_handle_is_invalid(source) || tc_widget_handle_is_invalid(target) || !resolver_ ||
            !resolver_(source) || (tc_ui_document_is_valid(document_) && !tc_ui_document_is_alive(document_, target))) {
            tc_log_error("[termin-gui-native] projection bridge rejected stale source or target handle");
            return false;
        }
        const auto duplicate = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) {
            return tc_widget_handle_eq(entry.target, target) && !same_source(entry.source, source);
        });
        if (duplicate != entries_.end()) {
            tc_log_error("[termin-gui-native] one widget cannot belong to conflicting scene projections");
            return false;
        }
        auto found = std::find_if(
            entries_.begin(), entries_.end(), [&](const Entry& entry) { return same_source(entry.source, source); });
        if (found != entries_.end()) {
            if (!tc_widget_handle_eq(found->target, target)) {
                detach_target(found->target);
                found->target = target;
            }
        } else {
            entries_.push_back({source, target});
        }
        return true;
    }

    bool WidgetSceneProjectionBridge::clear_projection(tc_graphic_item_handle source) {
        const auto found = std::find_if(
            entries_.begin(), entries_.end(), [&](const Entry& entry) { return same_source(entry.source, source); });
        if (found == entries_.end())
            return false;
        detach_target(found->target);
        entries_.erase(found);
        return true;
    }

    void WidgetSceneProjectionBridge::clear() {
        detach_all();
        entries_.clear();
    }

    std::size_t WidgetSceneProjectionBridge::size() const {
        return entries_.size();
    }

    void WidgetSceneProjectionBridge::detach_target(tc_widget_handle target) const {
        if (!tc_ui_document_is_valid(document_) || tc_widget_handle_is_invalid(host_))
            return;
        tc_widget* host = tc_ui_document_resolve_widget(document_, host_);
        tc_widget* widget = tc_ui_document_resolve_widget(document_, target);
        if (host && widget && widget->parent == host) {
            tc_widget_detach(widget);
            widget = tc_ui_document_resolve_widget(document_, target);
            if (widget)
                tc_widget_set_subtree_transform(widget, tc_ui_uniform_transform_identity());
        }
    }

    void WidgetSceneProjectionBridge::detach_all() {
        for (const Entry& entry : entries_)
            detach_target(entry.target);
    }

    void WidgetSceneProjectionBridge::reconcile() {
        if (!tc_ui_document_is_valid(document_) || tc_widget_handle_is_invalid(host_))
            return;
        tc_widget* host = tc_ui_document_resolve_widget(document_, host_);
        if (!host || !resolver_)
            return;
        std::vector<Entry> next;
        next.reserve(entries_.size());
        for (const Entry& entry : entries_) {
            const auto source = resolver_(entry.source);
            if (!source) {
                detach_target(entry.target);
                continue;
            }
            tc_widget* widget = tc_ui_document_resolve_widget(document_, entry.target);
            if (!widget) {
                tc_log_error("[termin-gui-native] projection bridge detached a stale widget handle");
                continue;
            }
            if (widget->parent && widget->parent != host) {
                tc_log_error("[termin-gui-native] projection bridge cannot steal a parented widget");
                continue;
            }
            tc_ui_uniform_transform uniform;
            if (!finite_bounds(source->local_bounds) ||
                !uniform_placement(tc_affine2f_mul(camera_, source->local_to_world), uniform)) {
                tc_log_error(
                    "[termin-gui-native] widget projection rejects rotation, shear, non-uniform or invalid placement");
                detach_target(entry.target);
                next.push_back(entry);
                continue;
            }
            if (!widget->parent && !tc_widget_append_child(host, widget)) {
                tc_log_error("[termin-gui-native] projection bridge failed to attach projected widget");
                continue;
            }
            next.push_back(entry);
        }
        entries_ = std::move(next);
    }

    void WidgetSceneProjectionBridge::layout() {
        reconcile();
        if (!tc_ui_document_is_valid(document_) || tc_widget_handle_is_invalid(host_))
            return;
        tc_widget* host = tc_ui_document_resolve_widget(document_, host_);
        if (!host || !resolver_)
            return;
        for (const Entry& entry : entries_) {
            const auto source = resolver_(entry.source);
            tc_widget* widget = tc_ui_document_resolve_widget(document_, entry.target);
            if (!source || !widget || widget->parent != host || !source->visible)
                continue;
            const tc_affine2f placement = tc_affine2f_mul(camera_, source->local_to_world);
            tc_ui_uniform_transform uniform;
            if (!finite_bounds(source->local_bounds) || !uniform_placement(placement, uniform)) {
                detach_target(entry.target);
                continue;
            }
            detail::layout_widget(widget,
                                  document_,
                                  {source->local_bounds.x0,
                                   source->local_bounds.y0,
                                   std::max(1.0f, source->local_bounds.x1 - source->local_bounds.x0),
                                   std::max(1.0f, source->local_bounds.y1 - source->local_bounds.y0)});
            tc_widget_set_subtree_transform(widget, uniform);
        }
    }

    void WidgetSceneProjectionBridge::paint_source(tc_graphic_item_handle source_handle, tc_ui_paint_context* context) {
        if (!tc_ui_document_is_valid(document_) || tc_widget_handle_is_invalid(host_))
            return;
        tc_widget* host = tc_ui_document_resolve_widget(document_, host_);
        if (!host || !resolver_ || !context)
            return;
        const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& candidate) {
            return same_source(candidate.source, source_handle);
        });
        if (entry == entries_.end())
            return;
        const auto source = resolver_(entry->source);
        tc_widget* widget = tc_ui_document_resolve_widget(document_, entry->target);
        if (source && source->visible && widget && widget->parent == host) {
            tc_ui_painter_push_clip(context, host->bounds);
            detail::paint_widget(widget, document_, context);
            tc_ui_painter_pop_clip(context);
        }
    }

    tc_widget_handle WidgetSceneProjectionBridge::hit_test_source(tc_graphic_item_handle source_handle,
                                                                  float parent_x,
                                                                  float parent_y) const {
        if (!tc_ui_document_is_valid(document_) || tc_widget_handle_is_invalid(host_))
            return tc_widget_handle_invalid();
        tc_widget* host = tc_ui_document_resolve_widget(document_, host_);
        if (!host || !resolver_ || !detail::rect_contains(host->bounds, parent_x, parent_y))
            return tc_widget_handle_invalid();
        const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& candidate) {
            return same_source(candidate.source, source_handle);
        });
        if (entry == entries_.end())
            return tc_widget_handle_invalid();
        const auto source = resolver_(entry->source);
        tc_widget* widget = tc_ui_document_resolve_widget(document_, entry->target);
        if (source && source->visible && source->enabled && widget && widget->parent == host)
            return detail::hit_test_widget(widget, document_, parent_x, parent_y);
        return tc_widget_handle_invalid();
    }

} // namespace termin::gui_native
