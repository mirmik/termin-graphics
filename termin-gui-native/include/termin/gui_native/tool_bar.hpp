#pragma once

#include <memory>
#include <string>
#include <vector>

#include <termin/gui_native/command_model.hpp>
#include <termin/gui_native/native_widget.hpp>

namespace termin::gui_native {

class ToolBar final : public NativeWidget {
  private:
    std::shared_ptr<CommandModel> model_;
    uint64_t observed_revision_ = 0;
    size_t model_connection_ = 0;
    std::vector<tc_ui_rect> item_rects_;
    float item_height_ = 32.0f;
    float padding_ = 4.0f;
    float separator_margin_ = 6.0f;
    float separator_width_ = 1.0f;
    float icon_gap_ = 4.0f;
    bool centered_ = false;
    size_t hovered_ = SIZE_MAX;
    size_t pressed_ = SIZE_MAX;
    Signal<ToolBar&, size_t, CommandId, const CommandData&> activated_;

  public:
    explicit ToolBar(std::shared_ptr<CommandModel> model = {});
    ~ToolBar() override;

    const std::shared_ptr<CommandModel>& model() const { return model_; }
    void set_model(std::shared_ptr<CommandModel> model);
    const std::vector<tc_ui_rect>& item_rects() const { return item_rects_; }
    size_t hovered_index() const { return hovered_; }
    std::string hovered_tooltip() const;
    void set_item_height(float height);
    float item_height() const { return item_height_; }
    void set_padding(float padding);
    float padding() const { return padding_; }
    void set_centered(bool centered);
    bool centered() const { return centered_; }

    Signal<ToolBar&, size_t, CommandId, const CommandData&>& activated() { return activated_; }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document,
                                     const tc_ui_pointer_event* event) override;

  private:
    void connect_model();
    void disconnect_model();
    void on_model_changed(const CommandChange& change);
    void sync_model();
    void compute_item_rects(tc_ui_document* document);
    size_t index_at(float x, float y) const;
    bool activate(size_t index);

};

} // namespace termin::gui_native
