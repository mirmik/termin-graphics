#pragma once

#include <cstddef>
#include <vector>

namespace termin::gui_native {
enum class SelectionMode { Single, Multiple };
class SelectionModel {
public:
    static constexpr size_t npos = static_cast<size_t>(-1);

private:
    SelectionMode mode_ = SelectionMode::Single;
    std::vector<size_t> selected_;
    size_t current_ = npos;
    size_t anchor_ = npos;

public:
    explicit SelectionModel(SelectionMode mode = SelectionMode::Single) : mode_(mode) {}
    SelectionMode mode() const { return mode_; }
    void set_mode(SelectionMode mode);
    size_t current() const { return current_; }
    size_t anchor() const { return anchor_; }
    const std::vector<size_t>& selected_indices() const { return selected_; }
    bool contains(size_t index) const;
    bool reconcile(size_t item_count);
    bool clear();
    bool select_only(size_t index);
    bool toggle(size_t index);
    bool extend_to(size_t index, bool additive = false);
    bool select_all(size_t item_count);
    bool items_inserted(size_t index, size_t count);
    bool items_erased(size_t index, size_t count, size_t remaining_count);
    void set_current(size_t index) { current_ = index; }
private:
    void normalize();
};
} // namespace termin::gui_native
