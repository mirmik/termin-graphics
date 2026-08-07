#include <termin/gui_native/wrap_layout.hpp>

#include "widgets_internal.hpp"

namespace termin::gui_native {
    using namespace detail;

    namespace {

        struct FlowItem {
            tc_widget* widget = nullptr;
            tc_ui_size measured{};
            tc_ui_insets margin{};
            size_t line = 0;
        };

        struct FlowLine {
            size_t first = 0;
            size_t count = 0;
            float primary = 0.0f;
            float cross = 0.0f;
        };

        struct FlowPlan {
            std::vector<FlowItem> items;
            std::vector<FlowLine> lines;
            float primary = 0.0f;
            float cross = 0.0f;
        };

        float horizontal_margin(tc_ui_insets margin) {
            return margin.left + margin.right;
        }

        float vertical_margin(tc_ui_insets margin) {
            return margin.top + margin.bottom;
        }

        FlowPlan build_plan(WrapLayout& owner,
                            tc_ui_document_handle document,
                            float available_primary,
                            float available_cross,
                            bool primary_definite,
                            bool cross_definite) {
            const bool horizontal = owner.orientation() == Orientation::Horizontal;
            FlowPlan plan;
            FlowLine line;
            line.first = 0;
            for (size_t index = 0; index < owner.child_count(); ++index) {
                tc_widget* child = owner.child_at(index);
                if (!child || !tc_widget_is_visible(child)) {
                    continue;
                }
                const tc_ui_widget_layout_spec spec = tc_widget_layout_spec(child);
                const tc_ui_insets margin = spec.margin;
                tc_ui_constraints child_constraints = unconstrained();
                if (primary_definite) {
                    if (horizontal) {
                        child_constraints.max_size.width =
                            std::max(0.0f, available_primary - horizontal_margin(margin));
                    } else {
                        child_constraints.max_size.height = std::max(0.0f, available_primary - vertical_margin(margin));
                    }
                }
                if (cross_definite) {
                    if (horizontal) {
                        child_constraints.max_size.height = std::max(0.0f, available_cross - vertical_margin(margin));
                    } else {
                        child_constraints.max_size.width = std::max(0.0f, available_cross - horizontal_margin(margin));
                    }
                }
                const tc_ui_size measured = measure_widget(child,
                                                           document,
                                                           child_constraints,
                                                           horizontal ? tc_ui_size{available_primary, available_cross}
                                                                      : tc_ui_size{available_cross, available_primary},
                                                           horizontal ? primary_definite : cross_definite,
                                                           horizontal ? cross_definite : primary_definite);
                const float item_primary = primary_size(measured, owner.orientation()) +
                                           (horizontal ? horizontal_margin(margin) : vertical_margin(margin));
                const float item_cross = cross_size(measured, owner.orientation()) +
                                         (horizontal ? vertical_margin(margin) : horizontal_margin(margin));
                const float separator = line.count > 0 ? owner.spacing() : 0.0f;
                if (primary_definite && line.count > 0 && line.primary + separator + item_primary > available_primary) {
                    plan.primary = std::max(plan.primary, line.primary);
                    plan.lines.push_back(line);
                    line = FlowLine{plan.items.size(), 0, 0.0f, 0.0f};
                }
                FlowItem item;
                item.widget = child;
                item.measured = measured;
                item.margin = margin;
                item.line = plan.lines.size();
                plan.items.push_back(item);
                line.primary += (line.count > 0 ? owner.spacing() : 0.0f) + item_primary;
                line.cross = std::max(line.cross, item_cross);
                ++line.count;
            }
            if (line.count > 0) {
                plan.primary = std::max(plan.primary, line.primary);
                plan.lines.push_back(line);
            }
            for (size_t index = 0; index < plan.lines.size(); ++index) {
                plan.cross += plan.lines[index].cross;
                if (index > 0) {
                    plan.cross += owner.line_spacing();
                }
            }
            return plan;
        }

    } // namespace

    WrapLayout::WrapLayout(Orientation orientation, const char* debug_name)
        : NativeWidget(debug_name ? debug_name : "WrapLayout"),
          orientation_(orientation) {
        set_mouse_transparent(true);
    }

    WrapLayout& WrapLayout::set_orientation(Orientation orientation) {
        if (orientation_ != orientation) {
            orientation_ = orientation;
            mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
        }
        return *this;
    }

    WrapLayout& WrapLayout::set_padding(EdgeInsets padding) {
        padding_ = padding;
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
        return *this;
    }

    WrapLayout& WrapLayout::set_spacing(float spacing) {
        spacing_ = std::max(0.0f, spacing);
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT);
        return *this;
    }

    WrapLayout& WrapLayout::set_line_spacing(float spacing) {
        line_spacing_ = std::max(0.0f, spacing);
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT);
        return *this;
    }

    WrapLayout& WrapLayout::set_line_alignment(CrossAxisAlignment alignment) {
        line_alignment_ = alignment == CrossAxisAlignment::Auto ? CrossAxisAlignment::Start : alignment;
        mark_dirty(TC_WIDGET_DIRTY_LAYOUT);
        return *this;
    }

    void WrapLayout::add_child(tc_widget_handle handle) {
        if (attach_child(c_widget(), handle, SIZE_MAX, "WrapLayout::add_child")) {
            mark_dirty(TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT);
        }
    }

    std::vector<tc_widget_handle> WrapLayout::children() const {
        std::vector<tc_widget_handle> result;
        result.reserve(child_count());
        for (size_t index = 0; index < child_count(); ++index) {
            if (const tc_widget* child = child_at(index)) {
                result.push_back(child->handle);
            }
        }
        return result;
    }

    tc_ui_size WrapLayout::measure(tc_ui_document_handle document, tc_ui_constraints constraints) {
        const bool horizontal = orientation_ == Orientation::Horizontal;
        const float primary_padding = horizontal ? padding_.left + padding_.right : padding_.top + padding_.bottom;
        const float cross_padding = horizontal ? padding_.top + padding_.bottom : padding_.left + padding_.right;
        const float maximum_primary = primary_size(constraints.max_size, orientation_);
        const float maximum_cross = cross_size(constraints.max_size, orientation_);
        const bool primary_definite = maximum_primary > 0.0f && maximum_primary < kHuge;
        const bool cross_definite = maximum_cross > 0.0f && maximum_cross < kHuge;
        const float available_primary = primary_definite ? std::max(0.0f, maximum_primary - primary_padding) : kHuge;
        const float available_cross = cross_definite ? std::max(0.0f, maximum_cross - cross_padding) : kHuge;
        const FlowPlan plan =
            build_plan(*this, document, available_primary, available_cross, primary_definite, cross_definite);
        const tc_ui_size measured = horizontal ? tc_ui_size{plan.primary + primary_padding, plan.cross + cross_padding}
                                               : tc_ui_size{plan.cross + cross_padding, plan.primary + primary_padding};
        return clamp_size(measured, constraints);
    }

    void WrapLayout::layout(tc_ui_document_handle document, tc_ui_rect rect) {
        NativeWidget::layout(document, rect);
        const bool horizontal = orientation_ == Orientation::Horizontal;
        const tc_ui_rect content = inset_rect(rect, padding_);
        const float available_primary = horizontal ? content.width : content.height;
        const float available_cross = horizontal ? content.height : content.width;
        const FlowPlan plan = build_plan(*this, document, available_primary, available_cross, true, true);
        float cross_cursor = horizontal ? content.y : content.x;
        for (const FlowLine& line : plan.lines) {
            float primary_cursor = horizontal ? content.x : content.y;
            for (size_t offset = 0; offset < line.count; ++offset) {
                const FlowItem& item = plan.items[line.first + offset];
                const float item_primary = primary_size(item.measured, orientation_);
                float item_cross = cross_size(item.measured, orientation_);
                const float primary_before = horizontal ? item.margin.left : item.margin.top;
                const float primary_after = horizontal ? item.margin.right : item.margin.bottom;
                const float cross_before = horizontal ? item.margin.top : item.margin.left;
                const float cross_after = horizontal ? item.margin.bottom : item.margin.right;
                const float usable_cross = std::max(0.0f, line.cross - cross_before - cross_after);
                float cross_offset = cross_before;
                if (line_alignment_ == CrossAxisAlignment::Stretch) {
                    item_cross = usable_cross;
                } else if (line_alignment_ == CrossAxisAlignment::Center) {
                    cross_offset = cross_before + std::max(0.0f, usable_cross - item_cross) * 0.5f;
                } else if (line_alignment_ == CrossAxisAlignment::End) {
                    cross_offset = cross_before + std::max(0.0f, usable_cross - item_cross);
                }
                const tc_ui_rect child_rect =
                    horizontal
                        ? tc_ui_rect{primary_cursor + primary_before,
                                     cross_cursor + cross_offset,
                                     item_primary,
                                     item_cross}
                        : tc_ui_rect{
                              cross_cursor + cross_offset, primary_cursor + primary_before, item_cross, item_primary};
                layout_widget(item.widget, document, child_rect);
                primary_cursor += primary_before + item_primary + primary_after + spacing_;
            }
            cross_cursor += line.cross + line_spacing_;
        }
    }

    void WrapLayout::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
        tc_ui_painter_push_clip(context, bounds());
        for (size_t index = 0; index < child_count(); ++index) {
            paint_widget(child_at(index), document, context);
        }
        tc_ui_painter_pop_clip(context);
    }

    tc_ui_event_result WrapLayout::pointer_event(tc_ui_document_handle, const tc_ui_pointer_event*) {
        return TC_UI_EVENT_IGNORED;
    }

    tc_widget_handle WrapLayout::hit_test(tc_ui_document_handle document, float x, float y) {
        if (!visible() || !rect_contains(bounds(), x, y)) {
            return tc_widget_handle_invalid();
        }
        for (size_t index = child_count(); index > 0; --index) {
            tc_widget* child = child_at(index - 1);
            if (!child || !tc_widget_is_visible(child) || !child->vtable || !child->vtable->hit_test) {
                continue;
            }
            const tc_widget_handle hit = child->vtable->hit_test(child, document, x, y);
            if (!tc_widget_handle_is_invalid(hit)) {
                return hit;
            }
        }
        return mouse_transparent() ? tc_widget_handle_invalid() : handle();
    }

} // namespace termin::gui_native
