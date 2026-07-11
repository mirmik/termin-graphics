#pragma once

#include <memory>
#include <utility>

#include <termin/gui_native/collection_model.hpp>
#include <termin/gui_native/native_widget.hpp>
#include <termin/gui_native/selection_model.hpp>

namespace termin::gui_native {
class ListWidget : public NativeWidget {
private:
    std::shared_ptr<CollectionModel> model_;
    SelectionModel selection_;
    uint64_t observed_revision_ = 0;
    size_t model_connection_ = 0;
    float row_height_ = 40.0f;
    float row_spacing_ = 2.0f;
    float scroll_y_ = 0.0f;
    float wheel_rows_ = 3.0f;
    float item_padding_ = 10.0f;
    size_t hovered_ = SelectionModel::npos;
    Signal<ListWidget&, const std::vector<size_t>&> selection_changed_;
    Signal<ListWidget&, size_t, const CollectionItem&> activated_;
    Signal<ListWidget&, int64_t, float, float> context_menu_requested_;

public:
    explicit ListWidget(std::shared_ptr<CollectionModel> model = {});
    ~ListWidget() override;
    const std::shared_ptr<CollectionModel>& model() const { return model_; }
    void set_model(std::shared_ptr<CollectionModel> model);
    SelectionModel& selection() { return selection_; }
    const SelectionModel& selection() const { return selection_; }
    void set_selection_mode(SelectionMode mode);
    void set_row_height(float height);
    void set_row_spacing(float spacing);
    void set_scroll_y(float offset);
    float scroll_y() const { return scroll_y_; }
    float content_height() const;
    size_t hovered_index() const { return hovered_; }
    std::pair<size_t, size_t> visible_range() const;
    void ensure_visible(size_t index);
    bool select_index(size_t index, bool toggle = false, bool extend = false, bool additive = false);
    bool clear_selection();
    Signal<ListWidget&, const std::vector<size_t>&>& selection_changed() { return selection_changed_; }
    Signal<ListWidget&, size_t, const CollectionItem&>& activated() { return activated_; }
    Signal<ListWidget&, int64_t, float, float>& context_menu_requested() {
        return context_menu_requested_;
    }
    tc_ui_size measure(tc_ui_document* document, tc_ui_constraints constraints) override;
    void layout(tc_ui_document* document, tc_ui_rect rect) override;
    void paint(tc_ui_document* document, tc_ui_paint_context* context) override;
    tc_ui_event_result pointer_event(tc_ui_document* document, const tc_ui_pointer_event* event) override;
    tc_ui_event_result key_event(tc_ui_document* document, const tc_ui_key_event* event) override;
private:
    void sync_model();
    void clamp_scroll();
    size_t index_at(float x, float y) const;
    size_t next_enabled(size_t from, int direction) const;
    bool apply_selection(size_t index, int32_t modifiers);
    void emit_selection_changed();
    void connect_model();
    void disconnect_model();
    void on_model_changed(const CollectionChange& change);
};
} // namespace termin::gui_native
