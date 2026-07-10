#include "widgets_internal.hpp"

namespace termin::gui_native {
using namespace detail;

const CollectionItem& CollectionModel::item(size_t index) const {
    if (index >= items_.size()) {
        throw std::out_of_range("collection item index out of range");
    }
    return items_[index];
}

void CollectionModel::validate_item(const CollectionItem& item) {
    if (!valid_utf8(item.stable_id) || !valid_utf8(item.text) || !valid_utf8(item.subtitle)) {
        tc_log_error("[termin-gui-native] collection model rejected invalid UTF-8 item");
        throw std::invalid_argument("collection item strings must be valid UTF-8");
    }
}

void CollectionModel::set_items(std::vector<CollectionItem> items) {
    for (const CollectionItem& item : items) validate_item(item);
    items_ = std::move(items);
    notify(CollectionChange {CollectionChangeKind::Reset, 0, items_.size()});
}

void CollectionModel::append(CollectionItem item) {
    validate_item(item);
    const size_t index = items_.size();
    items_.push_back(std::move(item));
    notify(CollectionChange {CollectionChangeKind::Insert, index, 1});
}

void CollectionModel::update(size_t index, CollectionItem item) {
    validate_item(item);
    if (index >= items_.size()) {
        throw std::out_of_range("collection item index out of range");
    }
    items_[index] = std::move(item);
    notify(CollectionChange {CollectionChangeKind::Update, index, 1});
}

void CollectionModel::erase(size_t index) {
    if (index >= items_.size()) {
        throw std::out_of_range("collection item index out of range");
    }
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    notify(CollectionChange {CollectionChangeKind::Erase, index, 1});
}

void CollectionModel::clear() {
    if (items_.empty()) return;
    const size_t count = items_.size();
    items_.clear();
    notify(CollectionChange {CollectionChangeKind::Reset, 0, count});
}

void CollectionModel::notify(CollectionChange change) {
    ++revision_;
    changed_.emit(*this, change);
}


} // namespace termin::gui_native
