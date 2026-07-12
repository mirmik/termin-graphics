#pragma once

#include <memory>
#include <string>
#include <utility>

#include <termin/gui_native/collection_model.hpp>
#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/selection_model.hpp>

namespace termin::gui_native {

class FileGridWidget final : public NativeWidget {
  private:
    std::shared_ptr<CollectionModel> model_;
    SelectionModel selection_;
    uint64_t observed_revision_ = 0;
    size_t model_connection_ = 0;
    float tile_width_ = 86.0f;
    float tile_height_ = 82.0f;
    float tile_spacing_ = 8.0f;
    float padding_ = 8.0f;
    float icon_size_ = 34.0f;
    float scrollbar_width_ = 8.0f;
    float scroll_y_ = 0.0f;
    bool show_scrollbar_ = true;
    bool dragging_scrollbar_ = false;
    size_t pressed_item_ = SelectionModel::npos;
    bool dragging_item_ = false;
    float item_press_x_ = 0.0f;
    float item_press_y_ = 0.0f;
    float drag_start_y_ = 0.0f;
    float drag_start_scroll_ = 0.0f;
    size_t hovered_ = SelectionModel::npos;
    std::string empty_text_ = "No items";
    Signal<FileGridWidget&, const std::vector<size_t>&> selection_changed_;
    Signal<FileGridWidget&, size_t, const CollectionItem&> activated_;
    Signal<FileGridWidget&, size_t, const CollectionItem&> delete_requested_;
    Signal<FileGridWidget&, int64_t, float, float> context_menu_requested_;
    Signal<FileGridWidget&, size_t, float, float, int32_t> drag_requested_;

  public:
    explicit FileGridWidget(std::shared_ptr<CollectionModel> model = {});
    ~FileGridWidget() override;

    const std::shared_ptr<CollectionModel>& model() const { return model_; }
    void set_model(std::shared_ptr<CollectionModel> model);
    SelectionModel& selection() { return selection_; }
    const SelectionModel& selection() const { return selection_; }
    void set_selection_mode(SelectionMode mode);

    float tile_width() const { return tile_width_; }
    float tile_height() const { return tile_height_; }
    float tile_spacing() const { return tile_spacing_; }
    float padding() const { return padding_; }
    float icon_size() const { return icon_size_; }
    void set_tile_size(float width, float height);
    void set_tile_spacing(float spacing);
    void set_padding(float padding);
    void set_icon_size(float size);
    void set_show_scrollbar(bool show);
    bool show_scrollbar() const { return show_scrollbar_; }
    void set_scrollbar_width(float width);
    float scrollbar_width() const { return scrollbar_width_; }
    void set_empty_text(std::string text);
    const std::string& empty_text() const { return empty_text_; }

    size_t column_count() const;
    size_t row_count() const;
    float scroll_y() const { return scroll_y_; }
    void set_scroll_y(float offset);
    float content_height() const;
    bool has_scrollbar() const;
    tc_ui_rect scrollbar_thumb_rect() const;
    std::pair<size_t, size_t> visible_range() const;
    tc_ui_rect item_rect(size_t index) const;
    void ensure_visible(size_t index);
    bool select_index(size_t index, bool toggle = false, bool extend = false,
                      bool additive = false);
    bool clear_selection();

    Signal<FileGridWidget&, const std::vector<size_t>&>& selection_changed() {
        return selection_changed_;
    }
    Signal<FileGridWidget&, size_t, const CollectionItem&>& activated() { return activated_; }
    Signal<FileGridWidget&, size_t, const CollectionItem&>& delete_requested() {
        return delete_requested_;
    }
    Signal<FileGridWidget&, int64_t, float, float>& context_menu_requested() {
        return context_menu_requested_;
    }
    Signal<FileGridWidget&, size_t, float, float, int32_t>& drag_requested() {
        return drag_requested_;
    }

    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document,
                                     const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;

  private:
    void connect_model();
    void disconnect_model();
    void on_model_changed(const CollectionChange& change);
    void sync_model();
    void clamp_scroll();
    float max_scroll() const;
    bool scrollbar_hit(float x, float y) const;
    size_t index_at(float x, float y) const;
    size_t next_enabled(size_t from, int direction) const;
    bool apply_selection(size_t index, int32_t modifiers);
    void emit_selection_changed();

};

} // namespace termin::gui_native
