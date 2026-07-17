#include <termin/gui_native/frame_timeline.hpp>

#include "widgets_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <stdexcept>

namespace termin::gui_native {

namespace {
bool valid_sample(const FrameTimelineSample& sample) {
    return sample.stable_id >= 0 &&
           std::isfinite(sample.interval_ms) && sample.interval_ms >= 0.0f &&
           std::isfinite(sample.active_ms) && sample.active_ms >= 0.0f &&
           std::isfinite(sample.lateness_ms) && sample.lateness_ms >= 0.0f &&
           std::isfinite(sample.target_ms) && sample.target_ms >= 0.0f;
}

struct WidgetLifetimeToken {
    tc_ui_document* document = nullptr;
    tc_widget_handle handle = tc_widget_handle_invalid();
};

WidgetLifetimeToken lifetime_token(const FrameTimelineWidget& widget) {
    return WidgetLifetimeToken{widget.c_widget()->document, widget.handle()};
}

bool callback_target_alive(const WidgetLifetimeToken& token) {
    return !token.document || tc_ui_document_is_alive(token.document, token.handle);
}
} // namespace

void FrameTimelineModel::set_samples(std::vector<FrameTimelineSample> samples) {
    int64_t previous_id = -1;
    for (const FrameTimelineSample& sample : samples) {
        if (!valid_sample(sample) || sample.stable_id <= previous_id) {
            tc_log_error("[termin-gui-native] FrameTimelineModel rejected invalid sample");
            throw std::invalid_argument(
                "frame timeline samples require unique increasing non-negative ids and timings"
            );
        }
        previous_id = sample.stable_id;
    }
    samples_ = std::move(samples);
    ++revision_;
    changed_.emit(*this);
}

void FrameTimelineModel::append_samples(std::vector<FrameTimelineSample> samples,
                                        size_t max_samples) {
    int64_t previous_id = samples_.empty() ? -1 : samples_.back().stable_id;
    for (const FrameTimelineSample& sample : samples) {
        if (!valid_sample(sample) || sample.stable_id <= previous_id) {
            tc_log_error("[termin-gui-native] FrameTimelineModel rejected invalid append");
            throw std::invalid_argument(
                "appended frame timeline samples must have increasing ids after existing samples"
            );
        }
        previous_id = sample.stable_id;
    }
    if (samples.empty()) return;

    samples_.insert(samples_.end(),
                    std::make_move_iterator(samples.begin()),
                    std::make_move_iterator(samples.end()));
    if (max_samples > 0 && samples_.size() > max_samples) {
        const size_t overflow = samples_.size() - max_samples;
        samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(overflow));
    }
    ++revision_;
    changed_.emit(*this);
}

void FrameTimelineModel::clear() {
    if (samples_.empty()) return;
    samples_.clear();
    ++revision_;
    changed_.emit(*this);
}

FrameTimelineWidget::FrameTimelineWidget(std::shared_ptr<FrameTimelineModel> model)
    : NativeWidget("FrameTimelineWidget"),
      model_(model ? std::move(model) : std::make_shared<FrameTimelineModel>()) {
    set_style_role(TC_UI_STYLE_PANEL);
    set_focusable(true);
    set_preferred_size(tc_ui_size{800.0f, 220.0f});
    connect_model();
    on_model_changed();
}

FrameTimelineWidget::~FrameTimelineWidget() { disconnect_model(); }

void FrameTimelineWidget::connect_model() {
    if (model_ && model_connection_ == 0) {
        model_connection_ = model_->changed().connect(
            [this](FrameTimelineModel&) { on_model_changed(); }
        );
    }
}

void FrameTimelineWidget::disconnect_model() {
    if (model_ && model_connection_ != 0) model_->changed().disconnect(model_connection_);
    model_connection_ = 0;
}

void FrameTimelineWidget::set_model(std::shared_ptr<FrameTimelineModel> model) {
    disconnect_model();
    model_ = model ? std::move(model) : std::make_shared<FrameTimelineModel>();
    connect_model();
    on_model_changed();
}

void FrameTimelineWidget::set_window_size(size_t count) {
    count = std::clamp<size_t>(count, 8, 10000);
    if (window_size_ == count) return;
    window_size_ = count;
    clamp_view();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void FrameTimelineWidget::set_scroll_offset(size_t count) {
    follow_latest_ = false;
    scroll_offset_ = count;
    clamp_view();
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void FrameTimelineWidget::set_follow_latest(bool enabled) {
    if (follow_latest_ == enabled) return;
    follow_latest_ = enabled;
    if (enabled) {
        scroll_offset_ = 0;
        if (!model_->samples().empty()) select_index(model_->samples().size() - 1, false);
    }
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void FrameTimelineWidget::set_warning_ratio(float ratio) {
    if (!std::isfinite(ratio) || ratio <= 1.0f) {
        tc_log_error("[termin-gui-native] FrameTimelineWidget rejected warning ratio");
        throw std::invalid_argument("frame timeline warning ratio must be finite and greater than one");
    }
    warning_ratio_ = ratio;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

size_t FrameTimelineWidget::index_for_id(int64_t stable_id) const {
    const auto& samples = model_->samples();
    const auto found = std::find_if(samples.begin(), samples.end(),
                                    [stable_id](const FrameTimelineSample& sample) {
                                        return sample.stable_id == stable_id;
                                    });
    return found == samples.end() ? SIZE_MAX : static_cast<size_t>(found - samples.begin());
}

bool FrameTimelineWidget::select_index(size_t index, bool disable_follow) {
    if (index >= model_->samples().size()) return false;
    const int64_t stable_id = model_->samples()[index].stable_id;
    if (disable_follow) follow_latest_ = false;
    if (selected_id_ && *selected_id_ == stable_id) return false;
    selected_id_ = stable_id;
    const WidgetLifetimeToken token = lifetime_token(*this);
    selection_changed_.emit(*this, stable_id);
    if (!callback_target_alive(token)) return true;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    return true;
}

bool FrameTimelineWidget::select(int64_t stable_id) {
    const size_t index = index_for_id(stable_id);
    if (index == SIZE_MAX) return false;
    return select_index(index);
}

bool FrameTimelineWidget::clear_selection() {
    if (!selected_id_) return false;
    selected_id_.reset();
    const WidgetLifetimeToken token = lifetime_token(*this);
    selection_changed_.emit(*this, -1);
    if (!callback_target_alive(token)) return true;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    return true;
}

std::pair<size_t, size_t> FrameTimelineWidget::visible_range() const {
    const size_t count = model_->samples().size();
    const size_t offset = std::min(scroll_offset_, count);
    const size_t end = count - offset;
    const size_t begin = end > window_size_ ? end - window_size_ : 0;
    return {begin, end};
}

void FrameTimelineWidget::clamp_view() {
    const size_t count = model_->samples().size();
    const size_t max_offset = count > 0 ? count - 1 : 0;
    scroll_offset_ = std::min(scroll_offset_, max_offset);
    if (follow_latest_) scroll_offset_ = 0;
}

void FrameTimelineWidget::on_model_changed() {
    clamp_view();
    if (follow_latest_ && !model_->samples().empty()) {
        select_index(model_->samples().size() - 1, false);
    } else if (selected_id_ && index_for_id(*selected_id_) == SIZE_MAX) {
        clear_selection();
    }
    if (hovered_index_ >= model_->samples().size()) hovered_index_ = SIZE_MAX;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size FrameTimelineWidget::measure(tc_ui_document*, tc_ui_constraints constraints) {
    return detail::clamp_size(preferred_size(), constraints);
}

void FrameTimelineWidget::paint(tc_ui_document* document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const tc_ui_rect rect = bounds();
    tc_ui_painter_fill_rect(context, rect, style.background);
    const auto [begin, end] = visible_range();
    if (begin == end) {
        tc_ui_color muted = style.foreground;
        muted.a *= 0.6f;
        tc_ui_painter_draw_text(context, "No frame capture", {rect.x + 8.0f, rect.y + 24.0f},
                                style.font_size, muted);
        tc_ui_painter_stroke_rect(context, rect, style.border, style.border_width);
        return;
    }

    const float header_height = 28.0f;
    const tc_ui_rect graph{rect.x, rect.y + header_height, rect.width,
                           std::max(0.0f, rect.height - header_height)};
    float largest = 0.0f;
    float target = 0.0f;
    for (size_t index = begin; index < end; ++index) {
        largest = std::max(largest, model_->samples()[index].interval_ms);
        if (model_->samples()[index].stable_id == selected_id_.value_or(-1) || target <= 0.0f)
            target = std::max(target, model_->samples()[index].target_ms);
    }
    if (target <= 0.0f) target = 1000.0f / 60.0f;
    const float scale = std::max(largest, target * warning_ratio_);
    const float slot_width = graph.width / static_cast<float>(end - begin);
    const float bar_width = std::max(0.5f, slot_width - std::min(1.0f, slot_width * 0.15f));
    const auto y_for = [&](float value) {
        return graph.y + graph.height - std::min(value / scale, 1.0f) * graph.height;
    };
    tc_ui_color target_color = style.foreground;
    target_color.a *= 0.45f;
    tc_ui_painter_draw_line(context, {graph.x, y_for(target)},
                            {graph.x + graph.width, y_for(target)}, target_color, 1.0f);

    for (size_t index = begin; index < end; ++index) {
        const FrameTimelineSample& sample = model_->samples()[index];
        const float x = graph.x + static_cast<float>(index - begin) * slot_width;
        const float interval_height = std::min(graph.height, sample.interval_ms / scale * graph.height);
        tc_ui_color interval_color = sample.hitch
            ? tc_ui_color{0.82f, 0.27f, 0.24f, 1.0f}
            : tc_ui_color{0.35f, 0.67f, 0.83f, 1.0f};
        if (index == hovered_index_) interval_color.a = 0.78f;
        tc_ui_painter_fill_rect(context,
            {x, graph.y + graph.height - interval_height, bar_width, interval_height}, interval_color);
        const float active_height = std::min(graph.height, sample.active_ms / scale * graph.height);
        tc_ui_painter_fill_rect(context,
            {x + bar_width * 0.2f, graph.y + graph.height - active_height,
             bar_width * 0.6f, active_height},
            tc_ui_color{0.34f, 0.78f, 0.38f, 1.0f});
        if (sample.gap_before) {
            tc_ui_painter_draw_line(context, {x, graph.y}, {x, graph.y + graph.height},
                                    tc_ui_color{0.95f, 0.55f, 0.18f, 1.0f}, 2.0f);
        }
        if (selected_id_ && sample.stable_id == *selected_id_) {
            tc_ui_painter_stroke_rect(context,
                {x, graph.y, std::max(slot_width, 2.0f), graph.height}, style.accent, 2.0f);
        }
    }

    const FrameTimelineSample* detail = nullptr;
    if (hovered_index_ >= begin && hovered_index_ < end) detail = &model_->samples()[hovered_index_];
    else if (selected_id_) {
        const size_t selected = index_for_id(*selected_id_);
        if (selected != SIZE_MAX) detail = &model_->samples()[selected];
    }
    char label[160]{};
    if (detail) {
        std::snprintf(label, sizeof(label), "#%lld  interval %.2f ms  active %.2f ms  late %.2f ms",
                      static_cast<long long>(detail->stable_id), detail->interval_ms,
                      detail->active_ms, detail->lateness_ms);
    } else {
        std::snprintf(label, sizeof(label), "%zu frames", end - begin);
    }
    tc_ui_painter_draw_text(context, label, {rect.x + 6.0f, rect.y + 19.0f},
                            std::max(9.0f, style.font_size - 1.0f), style.foreground);
    tc_ui_painter_stroke_rect(context, rect, style.border, style.border_width);
}

size_t FrameTimelineWidget::index_at(float x, float y) const {
    if (!detail::rect_contains(bounds(), x, y) || y < bounds().y + 28.0f) return SIZE_MAX;
    const auto [begin, end] = visible_range();
    if (begin == end || bounds().width <= 0.0f) return SIZE_MAX;
    const float slot = bounds().width / static_cast<float>(end - begin);
    const size_t local = static_cast<size_t>(std::max(0.0f, x - bounds().x) / slot);
    return std::min(begin + local, end - 1);
}

tc_ui_event_result FrameTimelineWidget::pointer_event(tc_ui_document* document,
                                                      const tc_ui_pointer_event* event) {
    if (!event) return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_LEAVE) {
        hovered_index_ = SIZE_MAX;
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        return TC_UI_EVENT_IGNORED;
    }
    if (!detail::rect_contains(bounds(), event->x, event->y)) return TC_UI_EVENT_IGNORED;
    if (event->type == TC_UI_POINTER_WHEEL) {
        if ((event->modifiers & TC_UI_MOD_CTRL) != 0) {
            const int delta = event->wheel_y > 0.0f ? -12 : 12;
            set_window_size(static_cast<size_t>(std::max<int>(8, static_cast<int>(window_size_) + delta)));
        } else {
            const int64_t delta = event->wheel_y > 0.0f ? 12 : -12;
            const int64_t next = std::max<int64_t>(0, static_cast<int64_t>(scroll_offset_) + delta);
            set_scroll_offset(static_cast<size_t>(next));
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_MOVE) {
        const size_t next = index_at(event->x, event->y);
        if (next != hovered_index_) {
            hovered_index_ = next;
            mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
        }
        return TC_UI_EVENT_HANDLED;
    }
    if (event->type == TC_UI_POINTER_DOWN &&
        event->button == pointer_button_value(PointerButton::Left)) {
        tc_ui_document_set_focus(document, handle());
        return select_index(index_at(event->x, event->y))
            ? TC_UI_EVENT_HANDLED : TC_UI_EVENT_IGNORED;
    }
    return TC_UI_EVENT_IGNORED;
}

tc_ui_event_result FrameTimelineWidget::key_event(tc_ui_document*, const tc_ui_key_event* event) {
    if (!event || event->type != TC_UI_KEY_DOWN || model_->samples().empty())
        return TC_UI_EVENT_IGNORED;
    size_t index = selected_id_ ? index_for_id(*selected_id_) : SIZE_MAX;
    if (event->key == TC_UI_KEY_HOME) index = 0;
    else if (event->key == TC_UI_KEY_END) index = model_->samples().size() - 1;
    else if (event->key == TC_UI_KEY_LEFT)
        index = index == SIZE_MAX ? 0 : (index > 0 ? index - 1 : 0);
    else if (event->key == TC_UI_KEY_RIGHT)
        index = index == SIZE_MAX ? model_->samples().size() - 1
                                  : std::min(index + 1, model_->samples().size() - 1);
    else return TC_UI_EVENT_IGNORED;
    select_index(index);
    return TC_UI_EVENT_HANDLED;
}

} // namespace termin::gui_native
