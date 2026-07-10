#include "widgets_internal.hpp"
namespace {

bool same_selection(const std::vector<size_t>& lhs, const std::vector<size_t>& rhs) {
    return lhs == rhs;
}

} // namespace

namespace termin::gui_native {
using namespace detail;

void SelectionModel::normalize() {
    std::sort(selected_.begin(), selected_.end());
    selected_.erase(std::unique(selected_.begin(), selected_.end()), selected_.end());
    if (mode_ == SelectionMode::Single && selected_.size() > 1) {
        const size_t keep = contains(current_) ? current_ : selected_.front();
        selected_.assign(1, keep);
    }
}

bool SelectionModel::contains(size_t index) const {
    return std::binary_search(selected_.begin(), selected_.end(), index);
}

void SelectionModel::set_mode(SelectionMode mode) {
    if (mode_ == mode) return;
    mode_ = mode;
    normalize();
    if (selected_.empty()) {
        current_ = npos;
        anchor_ = npos;
    } else {
        current_ = selected_.front();
        if (!contains(anchor_)) anchor_ = selected_.front();
    }
}

bool SelectionModel::reconcile(size_t item_count) {
    const std::vector<size_t> before = selected_;
    selected_.erase(
        std::remove_if(selected_.begin(), selected_.end(), [item_count](size_t index) {
            return index >= item_count;
        }),
        selected_.end()
    );
    if (current_ >= item_count) current_ = item_count == 0 ? npos : item_count - 1;
    if (anchor_ >= item_count) anchor_ = selected_.empty() ? npos : selected_.front();
    return !same_selection(before, selected_);
}

bool SelectionModel::clear() {
    if (selected_.empty() && current_ == npos && anchor_ == npos) return false;
    selected_.clear();
    current_ = npos;
    anchor_ = npos;
    return true;
}

bool SelectionModel::select_only(size_t index) {
    if (index == npos) {
        tc_log_error("[termin-gui-native] selection model rejected invalid index");
        throw std::invalid_argument("selection index must not be npos");
    }
    const bool changed = selected_.size() != 1 || selected_.front() != index;
    selected_.assign(1, index);
    current_ = index;
    anchor_ = index;
    return changed;
}

bool SelectionModel::toggle(size_t index) {
    if (index == npos) {
        tc_log_error("[termin-gui-native] selection model rejected invalid index");
        throw std::invalid_argument("selection index must not be npos");
    }
    if (mode_ == SelectionMode::Single) return select_only(index);
    const auto found = std::lower_bound(selected_.begin(), selected_.end(), index);
    bool changed = true;
    if (found != selected_.end() && *found == index) selected_.erase(found);
    else selected_.insert(found, index);
    current_ = index;
    anchor_ = index;
    return changed;
}

bool SelectionModel::extend_to(size_t index, bool additive) {
    if (index == npos) {
        tc_log_error("[termin-gui-native] selection model rejected invalid index");
        throw std::invalid_argument("selection index must not be npos");
    }
    if (mode_ == SelectionMode::Single) return select_only(index);
    if (anchor_ == npos) anchor_ = current_ == npos ? index : current_;
    const std::vector<size_t> before = selected_;
    if (!additive) selected_.clear();
    const size_t first = std::min(anchor_, index);
    const size_t last = std::max(anchor_, index);
    for (size_t value = first; value <= last; ++value) selected_.push_back(value);
    current_ = index;
    normalize();
    return !same_selection(before, selected_);
}

bool SelectionModel::select_all(size_t item_count) {
    if (mode_ != SelectionMode::Multiple) return false;
    const std::vector<size_t> before = selected_;
    selected_.resize(item_count);
    for (size_t index = 0; index < item_count; ++index) selected_[index] = index;
    current_ = item_count == 0 ? npos : item_count - 1;
    anchor_ = item_count == 0 ? npos : 0;
    return !same_selection(before, selected_);
}

bool SelectionModel::items_inserted(size_t index, size_t count) {
    if (count == 0) return false;
    const std::vector<size_t> before = selected_;
    for (size_t& selected : selected_) {
        if (selected >= index) selected += count;
    }
    if (current_ != npos && current_ >= index) current_ += count;
    if (anchor_ != npos && anchor_ >= index) anchor_ += count;
    return before != selected_;
}

bool SelectionModel::items_erased(size_t index, size_t count, size_t remaining_count) {
    if (count == 0) return false;
    const size_t end = index > std::numeric_limits<size_t>::max() - count
        ? std::numeric_limits<size_t>::max()
        : index + count;
    const std::vector<size_t> before = selected_;
    selected_.erase(
        std::remove_if(selected_.begin(), selected_.end(), [index, end](size_t selected) {
            return selected >= index && selected < end;
        }),
        selected_.end()
    );
    for (size_t& selected : selected_) {
        if (selected >= end) selected -= count;
    }
    const auto shift_position = [index, end, count, remaining_count](size_t position) {
        if (position == npos) return npos;
        if (position >= end) return position - count;
        if (position >= index) return remaining_count == 0 ? npos : std::min(index, remaining_count - 1);
        return position;
    };
    current_ = shift_position(current_);
    anchor_ = shift_position(anchor_);
    return before != selected_;
}


} // namespace termin::gui_native
