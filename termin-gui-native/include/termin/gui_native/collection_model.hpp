#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {
struct CollectionItem {
    std::string stable_id;
    std::string text;
    std::string subtitle;
    bool enabled = true;
    uint32_t texture_id = 0;
    // A lightweight semantic icon rendered by collection views.  Unlike texture_id this does
    // not require a renderer-owned GPU allocation, which makes it suitable for stable UI
    // affordances such as project-browser file types.
    std::string icon;

    CollectionItem() = default;
    CollectionItem(std::string stable_id_value, std::string text_value,
                   std::string subtitle_value = {}, bool enabled_value = true,
                   uint32_t texture_id_value = 0, std::string icon_value = {})
        : stable_id(std::move(stable_id_value)),
          text(std::move(text_value)),
          subtitle(std::move(subtitle_value)),
          enabled(enabled_value),
          texture_id(texture_id_value),
          icon(std::move(icon_value)) {}
};
enum class CollectionChangeKind { Reset, Insert, Update, Erase };
struct CollectionChange { CollectionChangeKind kind = CollectionChangeKind::Reset; size_t index = 0; size_t count = 0; };
class CollectionModel {
private:
    std::vector<CollectionItem> items_;
    uint64_t revision_ = 1;
    Signal<CollectionModel&, const CollectionChange&> changed_;

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
};
} // namespace termin::gui_native
