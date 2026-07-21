#include <termin/gui_native/frame_time_graph.hpp>

#include "widgets_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace termin::gui_native {

void FrameTimeModel::validate_sample(float milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds < 0.0f) {
        tc_log_error("[termin-gui-native] FrameTimeModel rejected invalid sample");
        throw std::invalid_argument("frame time samples must be finite and non-negative");
    }
}

void FrameTimeModel::trim() {
    if (samples_.size() <= max_samples_)
        return;
    samples_.erase(samples_.begin(), samples_.begin() + (samples_.size() - max_samples_));
}

void FrameTimeModel::notify() {
    ++revision_;
    changed_.emit(*this);
}

void FrameTimeModel::set_max_samples(size_t count) {
    if (count == 0) {
        tc_log_error("[termin-gui-native] FrameTimeModel rejected zero capacity");
        throw std::invalid_argument("frame time history capacity must be positive");
    }
    if (max_samples_ == count)
        return;
    max_samples_ = count;
    trim();
    notify();
}

void FrameTimeModel::add_sample(float milliseconds) {
    validate_sample(milliseconds);
    samples_.push_back(milliseconds);
    trim();
    notify();
}

void FrameTimeModel::set_samples(std::vector<float> samples) {
    for (float sample : samples)
        validate_sample(sample);
    samples_ = std::move(samples);
    trim();
    notify();
}

void FrameTimeModel::clear() {
    if (samples_.empty())
        return;
    samples_.clear();
    notify();
}

FrameTimeGraph::FrameTimeGraph(std::shared_ptr<FrameTimeModel> model)
    : NativeWidget("FrameTimeGraph"),
      model_(model ? std::move(model) : std::make_shared<FrameTimeModel>()) {
    set_style_role(TC_UI_STYLE_PANEL);
    set_preferred_size(tc_ui_size{300.0f, 80.0f});
    connect_model();
}

FrameTimeGraph::~FrameTimeGraph() { disconnect_model(); }

void FrameTimeGraph::connect_model() {
    if (!model_ || model_connection_ != 0)
        return;
    model_connection_ = model_->changed().connect([this](FrameTimeModel&) { on_model_changed(); });
}

void FrameTimeGraph::disconnect_model() {
    if (model_ && model_connection_ != 0)
        model_->changed().disconnect(model_connection_);
    model_connection_ = 0;
}

void FrameTimeGraph::on_model_changed() {
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

void FrameTimeGraph::set_model(std::shared_ptr<FrameTimeModel> model) {
    disconnect_model();
    model_ = model ? std::move(model) : std::make_shared<FrameTimeModel>();
    connect_model();
    on_model_changed();
}

void FrameTimeGraph::set_thresholds(float target_frame_ms, float warning_frame_ms) {
    if (!std::isfinite(target_frame_ms) || !std::isfinite(warning_frame_ms) ||
        target_frame_ms <= 0.0f || warning_frame_ms <= target_frame_ms) {
        tc_log_error("[termin-gui-native] FrameTimeGraph rejected invalid thresholds");
        throw std::invalid_argument("frame thresholds must be finite, positive and ordered");
    }
    target_frame_ms_ = target_frame_ms;
    warning_frame_ms_ = warning_frame_ms;
    mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
}

tc_ui_size FrameTimeGraph::measure(tc_ui_document_handle, tc_ui_constraints constraints) {
    return detail::clamp_size(preferred_size(), constraints);
}

void FrameTimeGraph::paint(tc_ui_document_handle document, tc_ui_paint_context* context) {
    const tc_ui_style style = computed_style(document);
    const tc_ui_rect rect = bounds();
    tc_ui_painter_fill_rect(context, rect, style.background);
    if (model_->samples().empty()) {
        tc_ui_text_metrics metrics{};
        const float font_size = std::max(8.0f, style.font_size - 2.0f);
        detail::measure_text(document, "No profiler data", font_size, metrics);
        tc_ui_painter_draw_text(context, "No profiler data",
                                tc_ui_point{rect.x + (rect.width - metrics.width) * 0.5f,
                                            rect.y + (rect.height + font_size) * 0.5f},
                                font_size, style.foreground);
        return;
    }

    const float largest = *std::max_element(model_->samples().begin(), model_->samples().end());
    const float scale = std::max(largest, warning_frame_ms_ + scale_headroom_ms_);
    const auto y_for = [&](float milliseconds) {
        return rect.y + rect.height - (milliseconds / scale) * rect.height;
    };
    tc_ui_color grid = style.border;
    grid.a *= 0.7f;
    tc_ui_color target = style.foreground;
    target.a *= 0.55f;
    for (float threshold : {target_frame_ms_, warning_frame_ms_}) {
        if (threshold < scale) {
            const float y = y_for(threshold);
            tc_ui_painter_draw_line(context, tc_ui_point{rect.x, y},
                                    tc_ui_point{rect.x + rect.width, y}, grid, 1.0f);
        }
    }
    const float target_y = y_for(target_frame_ms_);
    tc_ui_painter_draw_line(context, tc_ui_point{rect.x, target_y},
                            tc_ui_point{rect.x + rect.width, target_y}, target, 1.0f);

    const float label_size = std::max(7.0f, style.font_size - 4.0f);
    char target_label[16]{};
    char warning_label[16]{};
    std::snprintf(target_label, sizeof(target_label), "%.0f", 1000.0f / target_frame_ms_);
    std::snprintf(warning_label, sizeof(warning_label), "%.0f", 1000.0f / warning_frame_ms_);
    tc_ui_painter_draw_text(context, target_label, tc_ui_point{rect.x + 2.0f, target_y - 2.0f},
                            label_size, target);
    tc_ui_painter_draw_text(context, warning_label,
                            tc_ui_point{rect.x + 2.0f, y_for(warning_frame_ms_) - 2.0f}, label_size,
                            target);

    const float slot_width = rect.width / static_cast<float>(model_->max_samples());
    const float gap = std::min(1.0f, slot_width * 0.15f);
    const float bar_width = std::max(0.25f, slot_width - gap);
    const size_t count = model_->samples().size();
    for (size_t index = 0; index < count; ++index) {
        const float milliseconds = model_->samples()[index];
        const float height = std::min(rect.height, milliseconds / scale * rect.height);
        const float x =
            rect.x + static_cast<float>(model_->max_samples() - count + index) * slot_width;
        tc_ui_color color =
            milliseconds < target_frame_ms_
                ? tc_ui_color{0.31f, 0.71f, 0.31f, 1.0f}
                : (milliseconds < warning_frame_ms_ ? tc_ui_color{0.78f, 0.71f, 0.31f, 1.0f}
                                                    : tc_ui_color{0.78f, 0.31f, 0.31f, 1.0f});
        tc_ui_painter_fill_rect(
            context, tc_ui_rect{x, rect.y + rect.height - height, bar_width, height}, color);
    }
}

} // namespace termin::gui_native
