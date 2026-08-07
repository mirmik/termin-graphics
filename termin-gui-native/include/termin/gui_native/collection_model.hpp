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
        bool primary_toggle = false;
        bool primary_checked = false;
        std::string primary_toggle_label;
        bool secondary_toggle = false;
        bool secondary_checked = false;
        std::string secondary_toggle_label;

        CollectionItem() = default;
        CollectionItem(std::string stable_id_value,
                       std::string text_value,
                       std::string subtitle_value = {},
                       bool enabled_value = true,
                       uint32_t texture_id_value = 0,
                       std::string icon_value = {},
                       bool primary_toggle_value = false,
                       bool primary_checked_value = false,
                       std::string primary_toggle_label_value = {},
                       bool secondary_toggle_value = false,
                       bool secondary_checked_value = false,
                       std::string secondary_toggle_label_value = {})
            : stable_id(std::move(stable_id_value)),
              text(std::move(text_value)),
              subtitle(std::move(subtitle_value)),
              enabled(enabled_value),
              texture_id(texture_id_value),
              icon(std::move(icon_value)),
              primary_toggle(primary_toggle_value),
              primary_checked(primary_checked_value),
              primary_toggle_label(std::move(primary_toggle_label_value)),
              secondary_toggle(secondary_toggle_value),
              secondary_checked(secondary_checked_value),
              secondary_toggle_label(std::move(secondary_toggle_label_value)) {}
    };
    enum class CollectionChangeKind {
        Reset,
        Insert,
        Update,
        Erase
    };
    struct CollectionChange {
        CollectionChangeKind kind = CollectionChangeKind::Reset;
        size_t index = 0;
        size_t count = 0;
    };
    class CollectionModel {
    private:
        std::vector<CollectionItem> items_;
        uint64_t revision_ = 1;
        Signal<CollectionModel&, const CollectionChange&> changed_;

    public:
        size_t size() const {
            return items_.size();
        }
        bool empty() const {
            return items_.empty();
        }
        uint64_t revision() const {
            return revision_;
        }
        const CollectionItem& item(size_t index) const;
        const std::vector<CollectionItem>& items() const {
            return items_;
        }
        Signal<CollectionModel&, const CollectionChange&>& changed() {
            return changed_;
        }
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
