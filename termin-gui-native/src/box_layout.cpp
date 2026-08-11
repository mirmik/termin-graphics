#include "widgets_internal.hpp"

namespace termin::gui_native {
    using namespace detail;
    namespace {

        tc_ui_constraints box_child_constraints(tc_ui_size content_extent,
                                                Orientation orientation,
                                                bool stretch_cross,
                                                tc_ui_insets margin,
                                                float primary_extent = -1.0f) {
            const tc_ui_size available{std::max(0.0f, content_extent.width - margin.left - margin.right),
                                       std::max(0.0f, content_extent.height - margin.top - margin.bottom)};
            tc_ui_constraints constraints{tc_ui_size{0.0f, 0.0f}, available};
            if (orientation == Orientation::Vertical) {
                if (stretch_cross) {
                    constraints.min_size.width = available.width;
                }
                if (primary_extent >= 0.0f) {
                    constraints.min_size.height = primary_extent;
                    constraints.max_size.height = primary_extent;
                }
            } else {
                if (stretch_cross) {
                    constraints.min_size.height = available.height;
                }
                if (primary_extent >= 0.0f) {
                    constraints.min_size.width = primary_extent;
                    constraints.max_size.width = primary_extent;
                }
            }
            return constraints;
        }

        bool box_stretches_cross(CrossAxisAlignment alignment,
                                 Orientation orientation,
                                 const tc_ui_widget_layout_spec& spec) {
            const tc_ui_length cross_length = orientation == Orientation::Vertical ? spec.width : spec.height;
            return alignment == CrossAxisAlignment::Stretch &&
                   (cross_length.mode == TC_UI_LENGTH_AUTO || cross_length.mode == TC_UI_LENGTH_FILL);
        }

    } // namespace

    BoxLayout::BoxLayout(Orientation orientation, const char* debug_name)
        : NativeWidget(debug_name),
          orientation_(orientation) {}

    BoxLayout& BoxLayout::set_orientation(Orientation orientation) {
        orientation_ = orientation;
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
        return *this;
    }

    BoxLayout& BoxLayout::set_padding(EdgeInsets padding) {
        padding_ = padding;
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
        return *this;
    }

    BoxLayout& BoxLayout::set_spacing(float spacing) {
        spacing_ = std::max(0.0f, spacing);
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
        return *this;
    }

    BoxLayout& BoxLayout::set_cross_axis_alignment(CrossAxisAlignment alignment) {
        cross_axis_alignment_ = alignment == CrossAxisAlignment::Auto ? CrossAxisAlignment::Stretch : alignment;
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
        return *this;
    }

    BoxLayout& BoxLayout::set_background(SrgbColor color) {
        background_ = color;
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
        return *this;
    }

    BoxLayout& BoxLayout::set_border(SrgbColor color, float thickness) {
        border_ = color;
        border_thickness_ = std::max(0.0f, thickness);
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
        return *this;
    }

    BoxLayout& BoxLayout::set_corner_radius(float radius) {
        corner_radius_ = std::max(0.0f, radius);
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
        return *this;
    }

    void BoxLayout::add_child(tc_widget_handle handle) {
        add_child(handle, LayoutPolicy::Stretch);
    }

    void BoxLayout::add_child(tc_widget_handle handle, LayoutPolicy policy, float value) {
        if (tc_widget_handle_is_invalid(handle)) {
            tc_log_error("[termin-gui-native] cannot add invalid child handle to BoxLayout");
            return;
        }

        if (!attach_child(c_widget(), handle, SIZE_MAX, "BoxLayout::add_child")) {
            return;
        }
        items_.erase(std::remove_if(
                         items_.begin(),
                         items_.end(),
                         [handle](const LayoutItem& existing) { return tc_widget_handle_eq(existing.handle, handle); }),
                     items_.end());

        LayoutItem item{};
        item.handle = handle;
        item.policy = policy;
        if (policy == LayoutPolicy::Fixed) {
            item.fixed_extent = std::max(0.0f, value);
            item.grow = 0.0f;
            item.shrink = 0.0f;
            item.min_extent = item.fixed_extent;
            item.max_extent = item.fixed_extent;
        } else if (policy == LayoutPolicy::Preferred) {
            item.grow = 0.0f;
            item.shrink = 0.0f;
        } else if (policy == LayoutPolicy::Flex) {
            item.flex = value > 0.0f ? value : 1.0f;
            item.grow = item.flex;
            item.shrink = item.flex;
        } else {
            item.grow = 1.0f;
            item.shrink = 1.0f;
        }
        items_.push_back(item);
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
    }

    void BoxLayout::add_fixed_child(tc_widget_handle handle, float extent) {
        add_child(handle, LayoutPolicy::Fixed, extent);
    }

    void BoxLayout::add_preferred_child(tc_widget_handle handle) {
        add_child(handle, LayoutPolicy::Preferred);
    }

    void BoxLayout::add_flex_child(tc_widget_handle handle, float flex) {
        add_child(handle, LayoutPolicy::Flex, flex);
    }

    void BoxLayout::add_stretch_child(tc_widget_handle handle) {
        add_child(handle, LayoutPolicy::Stretch);
    }

    bool BoxLayout::set_child_placement(tc_widget_handle handle,
                                        LayoutPolicy policy,
                                        float basis,
                                        float grow,
                                        float shrink,
                                        float min_extent,
                                        float max_extent,
                                        CrossAxisAlignment align_self) {
        const float normalized_basis = std::max(0.0f, basis);
        const float normalized_min = std::max(0.0f, min_extent);
        const float normalized_max = std::max(0.0f, max_extent);
        if (policy == LayoutPolicy::Fixed &&
            (grow > 0.0f || shrink > 0.0f || (normalized_min > 0.0f && normalized_min != normalized_basis) ||
             (normalized_max > 0.0f && normalized_max != normalized_basis))) {
            tc_log_error("[termin-gui-native] fixed BoxLayout child placement conflicts with "
                         "grow, shrink or extent limits");
            return false;
        }
        if (policy != LayoutPolicy::Fixed && normalized_max > 0.0f && normalized_max < normalized_min) {
            tc_log_error("[termin-gui-native] BoxLayout child max extent is smaller than its "
                         "min extent");
            return false;
        }
        for (LayoutItem& item : items_) {
            if (!tc_widget_handle_eq(item.handle, handle)) {
                continue;
            }
            item.policy = policy;
            item.fixed_extent = policy == LayoutPolicy::Fixed ? normalized_basis : 0.0f;
            item.flex = std::max(0.0f, grow);
            item.grow = std::max(0.0f, grow);
            item.shrink = std::max(0.0f, shrink);
            item.min_extent = normalized_min;
            item.max_extent = normalized_max;
            if (policy == LayoutPolicy::Fixed) {
                item.min_extent = item.fixed_extent;
                item.max_extent = item.fixed_extent;
            }
            item.align_self = align_self;
            mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
            return true;
        }
        tc_log_error("[termin-gui-native] cannot configure a child that is not attached to "
                     "BoxLayout");
        return false;
    }

    bool BoxLayout::set_child_extent_limits(tc_widget_handle handle, float min_extent, float max_extent) {
        for (LayoutItem& item : items_) {
            if (item.handle.index == handle.index && item.handle.generation == handle.generation) {
                item.min_extent = std::max(0.0f, min_extent);
                item.max_extent = std::max(0.0f, max_extent);
                if (item.max_extent > 0.0f && item.max_extent < item.min_extent) {
                    item.max_extent = item.min_extent;
                }
                mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
                return true;
            }
        }
        return false;
    }

    std::vector<tc_widget_handle> BoxLayout::children() const {
        std::vector<tc_widget_handle> handles;
        handles.reserve(child_count());
        for (size_t index = 0; index < child_count(); ++index) {
            const tc_widget* child = child_at(index);
            if (child) {
                handles.push_back(child->handle);
            }
        }
        return handles;
    }

    tc_ui_size BoxLayout::measure(tc_ui_document_handle document, tc_ui_constraints constraints) {
        tc_ui_size content{0.0f, 0.0f};
        const tc_ui_size padding_extent{padding_.left + padding_.right, padding_.top + padding_.bottom};
        const tc_ui_size parent_extent{std::max(0.0f, constraints.max_size.width - padding_extent.width),
                                       std::max(0.0f, constraints.max_size.height - padding_extent.height)};
        const bool width_definite =
            constraints.max_size.width > 0.0f && constraints.min_size.width == constraints.max_size.width;
        const bool height_definite =
            constraints.max_size.height > 0.0f && constraints.min_size.height == constraints.max_size.height;
        size_t live_children = 0;
        for (size_t index = 0; index < child_count(); ++index) {
            tc_widget* child = child_at(index);
            if (!child || !tc_widget_is_visible(child)) {
                continue;
            }
            const LayoutItem* stored_item = find_layout_item(items_, child->handle);
            const LayoutItem default_item{child->handle, LayoutPolicy::Stretch};
            const LayoutItem& item = stored_item ? *stored_item : default_item;
            const CrossAxisAlignment alignment =
                item.align_self == CrossAxisAlignment::Auto ? cross_axis_alignment_ : item.align_self;
            const tc_ui_widget_layout_spec spec = tc_widget_layout_spec(child);
            const tc_ui_insets margin = spec.margin;
            const bool stretch_cross = box_stretches_cross(alignment, orientation_, spec);
            const float horizontal_margin = margin.left + margin.right;
            const float vertical_margin = margin.top + margin.bottom;
            tc_ui_constraints child_limits = unconstrained();
            child_limits.max_size = tc_ui_size{std::max(0.0f, parent_extent.width - horizontal_margin),
                                               std::max(0.0f, parent_extent.height - vertical_margin)};
            if (orientation_ == Orientation::Vertical && stretch_cross && width_definite) {
                child_limits.min_size.width = child_limits.max_size.width;
            } else if (orientation_ == Orientation::Horizontal && stretch_cross && height_definite) {
                child_limits.min_size.height = child_limits.max_size.height;
            }
            tc_ui_size child_size =
                measure_widget(child, document, child_limits, parent_extent, width_definite, height_definite);
            const float child_primary = item_basis(item, child_size, orientation_);
            if (orientation_ == Orientation::Vertical) {
                content.width = std::max(content.width, child_size.width + horizontal_margin);
                content.height += child_primary + vertical_margin;
            } else {
                content.width += child_primary + horizontal_margin;
                content.height = std::max(content.height, child_size.height + vertical_margin);
            }
            live_children += 1;
        }

        if (live_children > 1) {
            const float total_spacing = spacing_ * static_cast<float>(live_children - 1);
            if (orientation_ == Orientation::Vertical) {
                content.height += total_spacing;
            } else {
                content.width += total_spacing;
            }
        }

        content.width += padding_extent.width;
        content.height += padding_extent.height;
        content.width = std::max(content.width, preferred_size().width);
        content.height = std::max(content.height, preferred_size().height);
        content.width = std::max(content.width, min_size().width);
        content.height = std::max(content.height, min_size().height);
        const float min_width = std::max(min_size().width, constraints.min_size.width);
        const float min_height = std::max(min_size().height, constraints.min_size.height);
        const tc_ui_constraints effective_constraints{
            tc_ui_size{min_width, min_height},
            tc_ui_size{
                std::max(min_width,
                         std::min(effective_max(max_size().width), effective_max(constraints.max_size.width))),
                std::max(min_height,
                         std::min(effective_max(max_size().height), effective_max(constraints.max_size.height)))}};
        return clamp_size(content, effective_constraints);
    }

    void BoxLayout::layout(tc_ui_document_handle document, tc_ui_rect rect) {
        NativeWidget::layout(document, rect);

        struct LiveItem {
            tc_widget* widget = nullptr;
            tc_ui_size measured{};
            float extent = 0.0f;
            float min_extent = 0.0f;
            float max_extent = kHuge;
            float grow = 0.0f;
            float shrink = 0.0f;
            CrossAxisAlignment alignment = CrossAxisAlignment::Stretch;
            bool stretch_cross = true;
            tc_ui_insets margin{};
        };

        const tc_ui_rect content = inset_rect(rect, padding_);
        const tc_ui_size content_extent{content.width, content.height};
        std::vector<LiveItem> live_items;
        live_items.reserve(child_count());
        float base_extent = 0.0f;
        for (size_t index = 0; index < child_count(); ++index) {
            tc_widget* child = child_at(index);
            if (!child || !tc_widget_is_visible(child)) {
                continue;
            }
            const LayoutItem* stored_item = find_layout_item(items_, child->handle);
            const LayoutItem default_item{child->handle, LayoutPolicy::Stretch};
            const LayoutItem& item = stored_item ? *stored_item : default_item;
            NativeWidget* native = native_widget_body(child);
            const CrossAxisAlignment alignment =
                item.align_self == CrossAxisAlignment::Auto ? cross_axis_alignment_ : item.align_self;
            const tc_ui_widget_layout_spec spec = tc_widget_layout_spec(child);
            const tc_ui_insets margin = spec.margin;
            const bool stretch_cross = box_stretches_cross(alignment, orientation_, spec);
            tc_ui_size measured =
                measure_widget(child,
                               document,
                               box_child_constraints(content_extent, orientation_, stretch_cross, margin),
                               content_extent,
                               true,
                               true);
            const float primary_margin =
                orientation_ == Orientation::Vertical ? margin.top + margin.bottom : margin.left + margin.right;
            float child_min_extent = item_min_extent(item, native, orientation_);
            float child_max_extent = item_max_extent(item, native, orientation_);
            if (item.policy != LayoutPolicy::Fixed) {
                const float spec_min = orientation_ == Orientation::Vertical ? spec.min_height : spec.min_width;
                const float touch_min = spec.touch_target_policy == TC_UI_TOUCH_TARGET_LAYOUT_MINIMUM
                                            ? (orientation_ == Orientation::Vertical ? spec.minimum_touch_target.height
                                                                                     : spec.minimum_touch_target.width)
                                            : 0.0f;
                const float spec_max = orientation_ == Orientation::Vertical ? spec.max_height : spec.max_width;
                child_min_extent = std::max(child_min_extent, std::max(spec_min, touch_min));
                if (spec_max > 0.0f) {
                    child_max_extent = std::min(child_max_extent, spec_max);
                }
            }
            const float min_extent = child_min_extent + primary_margin;
            const float max_extent = std::max(min_extent, child_max_extent + primary_margin);
            LiveItem live{};
            live.widget = child;
            live.measured = measured;
            live.min_extent = min_extent;
            live.max_extent = max_extent;
            live.grow = std::max(0.0f, item.grow);
            live.shrink = std::max(0.0f, item.shrink);
            live.alignment = alignment;
            live.stretch_cross = stretch_cross;
            live.margin = margin;
            live.extent =
                clamp_float(item_basis(item, measured, orientation_) + primary_margin, min_extent, max_extent);
            base_extent += live.extent;
            live_items.push_back(live);
        }
        if (live_items.empty()) {
            return;
        }

        const float total_spacing = spacing_ * static_cast<float>(live_items.size() - 1);
        const float axis_extent = orientation_ == Orientation::Vertical ? content.height : content.width;
        const float available_extent = std::max(0.0f, axis_extent - total_spacing);
        std::vector<float> extents;
        std::vector<float> min_extents;
        std::vector<float> max_extents;
        std::vector<float> grow_weights;
        std::vector<float> shrink_weights;
        extents.reserve(live_items.size());
        min_extents.reserve(live_items.size());
        max_extents.reserve(live_items.size());
        grow_weights.reserve(live_items.size());
        shrink_weights.reserve(live_items.size());
        for (const LiveItem& live : live_items) {
            extents.push_back(live.extent);
            min_extents.push_back(live.min_extent);
            max_extents.push_back(live.max_extent);
            grow_weights.push_back(live.grow);
            shrink_weights.push_back(live.shrink);
        }

        if (available_extent > base_extent) {
            distribute_grow(extents, max_extents, grow_weights, available_extent - base_extent);
        } else if (available_extent < base_extent) {
            distribute_shrink(extents, min_extents, shrink_weights, base_extent - available_extent);
        }

        float cursor = orientation_ == Orientation::Vertical ? content.y : content.x;
        for (size_t i = 0; i < live_items.size(); ++i) {
            LiveItem& live = live_items[i];
            const float primary_margin = orientation_ == Orientation::Vertical ? live.margin.top + live.margin.bottom
                                                                               : live.margin.left + live.margin.right;
            const float child_primary = std::max(0.0f, extents[i] - primary_margin);
            live.measured = measure_widget(
                live.widget,
                document,
                box_child_constraints(content_extent, orientation_, live.stretch_cross, live.margin, child_primary),
                content_extent,
                true,
                true);
            tc_ui_rect child_rect{};
            const float cross_available = orientation_ == Orientation::Vertical
                                              ? std::max(0.0f, content.width - live.margin.left - live.margin.right)
                                              : std::max(0.0f, content.height - live.margin.top - live.margin.bottom);
            const float measured_cross = std::min(cross_available, cross_size(live.measured, orientation_));
            const float child_cross = live.stretch_cross ? cross_available : measured_cross;
            float cross_offset = 0.0f;
            if (live.alignment == CrossAxisAlignment::Center) {
                cross_offset = (cross_available - child_cross) * 0.5f;
            } else if (live.alignment == CrossAxisAlignment::End) {
                cross_offset = cross_available - child_cross;
            }
            if (orientation_ == Orientation::Vertical) {
                child_rect = tc_ui_rect{
                    content.x + live.margin.left + cross_offset, cursor + live.margin.top, child_cross, child_primary};
            } else {
                child_rect = tc_ui_rect{
                    cursor + live.margin.left, content.y + live.margin.top + cross_offset, child_primary, child_cross};
            }
            layout_widget(live.widget, document, child_rect);
            cursor += extents[i] + spacing_;
        }
    }

    void BoxLayout::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
        if (color_visible(background_)) {
            if (corner_radius_ > 0.0f) {
                tc_ui_painter_fill_rounded_rect(context, bounds(), corner_radius_, to_tc_ui_srgb(background_));
            } else {
                tc_ui_painter_fill_rect(context, bounds(), to_tc_ui_srgb(background_));
            }
        }
        if (color_visible(border_) && border_thickness_ > 0.0f) {
            if (corner_radius_ > 0.0f) {
                tc_ui_painter_stroke_rounded_rect(
                    context, bounds(), corner_radius_, to_tc_ui_srgb(border_), border_thickness_);
            } else {
                tc_ui_painter_stroke_rect(context, bounds(), to_tc_ui_srgb(border_), border_thickness_);
            }
        }

        tc_ui_painter_push_clip(context, bounds());
        for (size_t index = 0; index < child_count(); ++index) {
            paint_widget(child_at(index), document, context);
        }
        tc_ui_painter_pop_clip(context);
    }

    tc_ui_event_result BoxLayout::pointer_event(tc_ui_document_handle, const tc_ui_pointer_event* event) {
        (void)event;
        return TC_UI_EVENT_IGNORED;
    }

    tc_widget_handle BoxLayout::hit_test(tc_ui_document_handle document, float x, float y) {
        if (!visible() || !rect_contains(bounds(), x, y)) {
            return tc_widget_handle_invalid();
        }
        for (size_t index = child_count(); index > 0; --index) {
            tc_widget* child = child_at(index - 1);
            if (!child || !tc_widget_is_visible(child)) {
                continue;
            }
            tc_widget_handle hit = detail::hit_test_widget(child, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) {
                return hit;
            }
        }
        return mouse_transparent() ? tc_widget_handle_invalid() : handle();
    }

} // namespace termin::gui_native
