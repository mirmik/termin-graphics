#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {

struct FrameTimelineSample {
    int64_t stable_id = -1;
    float interval_ms = 0.0f;
    float active_ms = 0.0f;
    float lateness_ms = 0.0f;
    float target_ms = 0.0f;
    bool hitch = false;
    bool gap_before = false;
};

class FrameTimelineModel {
  private:
    std::vector<FrameTimelineSample> samples_;
    uint64_t revision_ = 1;
    Signal<FrameTimelineModel&> changed_;

  public:
    const std::vector<FrameTimelineSample>& samples() const { return samples_; }
    uint64_t revision() const { return revision_; }
    void set_samples(std::vector<FrameTimelineSample> samples);
    void append_samples(std::vector<FrameTimelineSample> samples, size_t max_samples = 0);
    void clear();
    Signal<FrameTimelineModel&>& changed() { return changed_; }
};

class FrameTimelineWidget final : public NativeWidget {
  private:
    std::shared_ptr<FrameTimelineModel> model_;
    size_t model_connection_ = 0;
    size_t window_size_ = 180;
    size_t scroll_offset_ = 0;
    size_t hovered_index_ = SIZE_MAX;
    std::optional<int64_t> selected_id_;
    bool follow_latest_ = true;
    float warning_ratio_ = 2.0f;
    Signal<FrameTimelineWidget&, int64_t> selection_changed_;

  public:
    explicit FrameTimelineWidget(std::shared_ptr<FrameTimelineModel> model = {});
    ~FrameTimelineWidget() override;

    const std::shared_ptr<FrameTimelineModel>& model() const { return model_; }
    void set_model(std::shared_ptr<FrameTimelineModel> model);
    size_t window_size() const { return window_size_; }
    void set_window_size(size_t count);
    size_t scroll_offset() const { return scroll_offset_; }
    void set_scroll_offset(size_t count);
    bool follow_latest() const { return follow_latest_; }
    void set_follow_latest(bool enabled);
    std::optional<int64_t> selected_id() const { return selected_id_; }
    bool select(int64_t stable_id);
    bool clear_selection();
    std::pair<size_t, size_t> visible_range() const;
    float warning_ratio() const { return warning_ratio_; }
    void set_warning_ratio(float ratio);
    Signal<FrameTimelineWidget&, int64_t>& selection_changed() { return selection_changed_; }

    tc_ui_size measure(tc_ui_document_handle document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document_handle document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document_handle document,
                                     const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document_handle document,
                                 const tc_ui_key_event* event) override;

  private:
    void connect_model();
    void disconnect_model();
    void on_model_changed();
    void clamp_view();
    size_t index_for_id(int64_t stable_id) const;
    size_t index_at(float x, float y) const;
    bool select_index(size_t index, bool disable_follow = true);
};

} // namespace termin::gui_native
