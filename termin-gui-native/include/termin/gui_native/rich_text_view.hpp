#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/rich_text_model.hpp>

namespace termin::gui_native {

class RichTextView : public NativeWidget {
  private:
    struct VisualRun {
        std::string text;
        RichTextStyle style;
        size_t source_start = 0;
        size_t source_end = 0;
        float width = 0.0f;
    };
    struct VisualRow {
        std::vector<VisualRun> runs;
        size_t source_start = 0;
        size_t source_end = 0;
        float width = 0.0f;
    };
    struct ScrollbarGeometry {
        bool visible = false;
        tc_ui_rect track{};
        tc_ui_rect thumb{};
        float max_scroll = 0.0f;
    };
    std::shared_ptr<RichTextModel> model_;
    size_t model_connection_ = 0;
    std::string placeholder_;
    bool word_wrap_ = true;
    bool show_scrollbar_ = true;
    float line_height_ = 0.0f;
    float scroll_y_ = 0.0f;
    float scrollbar_width_ = 8.0f;
    float minimum_thumb_height_ = 20.0f;
    bool selecting_ = false;
    bool dragging_scrollbar_ = false;
    float drag_start_y_ = 0.0f;
    float drag_start_scroll_ = 0.0f;
    size_t selection_anchor_ = SIZE_MAX;
    size_t selection_cursor_ = 0;
    std::vector<VisualRow> rows_;
    uint64_t cached_revision_ = 0;
    float cached_width_ = -1.0f;
    float cached_font_size_ = -1.0f;
    float cached_line_height_ = 0.0f;

  public:
    explicit RichTextView(std::shared_ptr<RichTextModel> model = {});
    ~RichTextView() override;

    const std::shared_ptr<RichTextModel>& model() const { return model_; }
    void set_model(std::shared_ptr<RichTextModel> model);

    const std::string& placeholder() const { return placeholder_; }
    void set_placeholder(std::string placeholder);
    bool word_wrap() const { return word_wrap_; }
    void set_word_wrap(bool enabled);
    bool show_scrollbar() const { return show_scrollbar_; }
    void set_show_scrollbar(bool enabled);
    float line_height() const { return line_height_; }
    void set_line_height(float height);
    float scroll_y() const { return scroll_y_; }
    void set_scroll_y(float offset);
    float content_height() const { return cached_line_height_ * static_cast<float>(rows_.size()); }
    size_t visual_line_count() const { return rows_.size(); }

    bool has_selection() const;
    size_t selection_start() const;
    size_t selection_end() const;
    std::string selected_text() const;
    void select(size_t anchor, size_t cursor);
    void select_all();
    void clear_selection();

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document,
                                     const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;

    void connect_model();
    void disconnect_model();
    void on_model_changed();
    void invalidate_visual_rows();
    tc_ui_rect content_rect(tc_ui_document* document) const;
    float effective_line_height(tc_ui_document* document) const;
    float measure_width(tc_ui_document* document, std::string_view text, float font_size) const;
    void ensure_visual_rows(tc_ui_document* document, float width, float font_size);
    tc_ui_rect prepare_layout(tc_ui_document* document, const tc_ui_style& style);
    void clamp_scroll(float viewport_height);
    ScrollbarGeometry scrollbar_geometry(tc_ui_rect content) const;
    size_t source_offset_from_point(tc_ui_document* document, tc_ui_rect content, float x,
                                    float y) const;
    float row_x_for_offset(tc_ui_document* document, const VisualRow& row, size_t offset,
                           float font_size) const;
    void append_run(VisualRow& row, std::string text, const RichTextStyle& style,
                    size_t source_start, size_t source_end, float width);

};

} // namespace termin::gui_native
