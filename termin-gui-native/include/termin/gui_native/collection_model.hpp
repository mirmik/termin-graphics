#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
struct CollectionItem { std::string stable_id; std::string text; std::string subtitle; bool enabled = true; };
enum class CollectionChangeKind { Reset, Insert, Update, Erase };
struct CollectionChange { CollectionChangeKind kind = CollectionChangeKind::Reset; size_t index = 0; size_t count = 0; };
class CollectionModel {
public:
    size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    uint64_t revision() const { return revision_; }
    const CollectionItem& item(size_t index) const;
    const std::vector<CollectionItem>& items() const { return items_; }
    Signal<CollectionModel&, const CollectionChange&>& changed() { return changed_; }
    void set_items(std::vector<CollectionItem> items);
    void append(CollectionItem item);
    void update(size_t index, CollectionItem item);
    void erase(size_t index);
    void clear();
private:
    static void validate_item(const CollectionItem& item);
    void notify(CollectionChange change);
    std::vector<CollectionItem> items_;
    uint64_t revision_ = 1;
    Signal<CollectionModel&, const CollectionChange&> changed_;
};
} // namespace termin::gui_native
