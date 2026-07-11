#pragma once

#include <memory>
#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {

class FrameTimeModel {
  private:
    size_t max_samples_ = 120;
    std::vector<float> samples_;
    uint64_t revision_ = 1;
    Signal<FrameTimeModel&> changed_;

  public:
    size_t max_samples() const { return max_samples_; }
    void set_max_samples(size_t count);
    const std::vector<float>& samples() const { return samples_; }
    uint64_t revision() const { return revision_; }
    void add_sample(float milliseconds);
    void set_samples(std::vector<float> samples);
    void clear();
    Signal<FrameTimeModel&>& changed() { return changed_; }

  private:
    static void validate_sample(float milliseconds);
    void trim();
    void notify();

};

class FrameTimeGraph : public NativeWidget {
  private:
    std::shared_ptr<FrameTimeModel> model_;
    size_t model_connection_ = 0;
    float target_frame_ms_ = 1000.0f / 60.0f;
    float warning_frame_ms_ = 1000.0f / 30.0f;
    float scale_headroom_ms_ = 5.0f;

  public:
    explicit FrameTimeGraph(std::shared_ptr<FrameTimeModel> model = {});
    ~FrameTimeGraph() override;
    const std::shared_ptr<FrameTimeModel>& model() const { return model_; }
    void set_model(std::shared_ptr<FrameTimeModel> model);
    float target_frame_ms() const { return target_frame_ms_; }
    float warning_frame_ms() const { return warning_frame_ms_; }
    void set_thresholds(float target_frame_ms, float warning_frame_ms);
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;

  private:
    void connect_model();
    void disconnect_model();
    void on_model_changed();
};

} // namespace termin::gui_native
