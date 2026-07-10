#include "widgets_internal.hpp"

#include <algorithm>
#include <stdexcept>

namespace termin::gui_native {
using namespace detail;

bool TreeModel::contains(TreeNodeId id) const {
    return id != kInvalidTreeNodeId && nodes_.find(id) != nodes_.end();
}

const TreeNode& TreeModel::node(TreeNodeId id) const {
    const auto found = nodes_.find(id);
    if (found == nodes_.end())
        throw std::out_of_range("tree node id is not live");
    return found->second;
}

const std::vector<TreeNodeId>& TreeModel::children(TreeNodeId parent) const {
    return sibling_list(parent);
}

void TreeModel::validate_item(const CollectionItem& item) {
    if (!valid_utf8(item.stable_id) || !valid_utf8(item.text) || !valid_utf8(item.subtitle)) {
        tc_log_error("[termin-gui-native] tree model rejected invalid UTF-8 item");
        throw std::invalid_argument("tree item strings must be valid UTF-8");
    }
}

std::vector<TreeNodeId>& TreeModel::sibling_list(TreeNodeId parent) {
    if (parent == kInvalidTreeNodeId)
        return roots_;
    auto found = nodes_.find(parent);
    if (found == nodes_.end()) {
        tc_log_error("[termin-gui-native] tree model rejected stale parent id");
        throw std::out_of_range("tree parent id is not live");
    }
    return found->second.children;
}

const std::vector<TreeNodeId>& TreeModel::sibling_list(TreeNodeId parent) const {
    if (parent == kInvalidTreeNodeId)
        return roots_;
    return node(parent).children;
}

TreeNodeId TreeModel::append_root(CollectionItem item) {
    return insert_child(kInvalidTreeNodeId, roots_.size(), std::move(item));
}

TreeNodeId TreeModel::append_child(TreeNodeId parent, CollectionItem item) {
    return insert_child(parent, sibling_list(parent).size(), std::move(item));
}

TreeNodeId TreeModel::insert_child(TreeNodeId parent, size_t index, CollectionItem item) {
    validate_item(item);
    std::vector<TreeNodeId>& siblings = sibling_list(parent);
    if (index > siblings.size())
        throw std::out_of_range("tree insertion index out of range");
    if (next_id_ == kInvalidTreeNodeId) {
        tc_log_error("[termin-gui-native] tree node id space exhausted");
        throw std::overflow_error("tree node id space exhausted");
    }
    const TreeNodeId id = next_id_++;
    TreeNode value{id, std::move(item), parent, {}};
    nodes_.emplace(id, std::move(value));
    try {
        siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(index), id);
    } catch (...) {
        nodes_.erase(id);
        throw;
    }
    notify(TreeChange{TreeChangeKind::Insert, id, parent, index, 1});
    return id;
}

void TreeModel::update(TreeNodeId id, CollectionItem item) {
    validate_item(item);
    auto found = nodes_.find(id);
    if (found == nodes_.end())
        throw std::out_of_range("tree node id is not live");
    found->second.item = std::move(item);
    notify(TreeChange{TreeChangeKind::Update, id, found->second.parent, 0, 1});
}

bool TreeModel::is_descendant(TreeNodeId candidate, TreeNodeId ancestor) const {
    TreeNodeId current = candidate;
    size_t guard = 0;
    while (current != kInvalidTreeNodeId) {
        if (current == ancestor)
            return true;
        current = node(current).parent;
        if (++guard > nodes_.size()) {
            tc_log_error("[termin-gui-native] tree model contains a parent cycle");
            throw std::logic_error("tree model contains a parent cycle");
        }
    }
    return false;
}

void TreeModel::move(TreeNodeId id, TreeNodeId new_parent, size_t index) {
    auto found = nodes_.find(id);
    if (found == nodes_.end())
        throw std::out_of_range("tree node id is not live");
    if (new_parent == id || (new_parent != kInvalidTreeNodeId && is_descendant(new_parent, id))) {
        tc_log_error("[termin-gui-native] tree model rejected a move that creates a cycle");
        throw std::invalid_argument("tree move would create a cycle");
    }
    std::vector<TreeNodeId>& destination = sibling_list(new_parent);
    const TreeNodeId old_parent = found->second.parent;
    std::vector<TreeNodeId>& source = sibling_list(old_parent);
    const auto source_it = std::find(source.begin(), source.end(), id);
    if (source_it == source.end()) {
        tc_log_error("[termin-gui-native] tree model parent link is not mirrored "
                     "by siblings");
        throw std::logic_error("tree parent link is inconsistent");
    }
    if (index == SIZE_MAX)
        index = destination.size();
    if (index > destination.size())
        throw std::out_of_range("tree move index out of range");
    const size_t source_index = static_cast<size_t>(source_it - source.begin());
    if (&source == &destination && index > source_index)
        --index;
    if (&source == &destination && index == source_index)
        return;
    if (&source != &destination)
        destination.reserve(destination.size() + 1);
    source.erase(source_it);
    destination.insert(destination.begin() + static_cast<std::ptrdiff_t>(index), id);
    found->second.parent = new_parent;
    notify(TreeChange{TreeChangeKind::Move, id, new_parent, index, 1});
}

void TreeModel::erase_subtree(TreeNodeId id, size_t& count) {
    auto found = nodes_.find(id);
    if (found == nodes_.end())
        return;
    const std::vector<TreeNodeId> children_copy = found->second.children;
    for (TreeNodeId child : children_copy)
        erase_subtree(child, count);
    nodes_.erase(id);
    ++count;
}

void TreeModel::erase(TreeNodeId id) {
    auto found = nodes_.find(id);
    if (found == nodes_.end())
        throw std::out_of_range("tree node id is not live");
    const TreeNodeId parent = found->second.parent;
    std::vector<TreeNodeId>& siblings = sibling_list(parent);
    const auto sibling = std::find(siblings.begin(), siblings.end(), id);
    if (sibling == siblings.end()) {
        tc_log_error("[termin-gui-native] tree model parent link is not mirrored "
                     "by siblings");
        throw std::logic_error("tree parent link is inconsistent");
    }
    const size_t index = static_cast<size_t>(sibling - siblings.begin());
    siblings.erase(sibling);
    size_t count = 0;
    erase_subtree(id, count);
    notify(TreeChange{TreeChangeKind::Erase, id, parent, index, count});
}

void TreeModel::clear() {
    if (nodes_.empty())
        return;
    const size_t count = nodes_.size();
    nodes_.clear();
    roots_.clear();
    notify(TreeChange{TreeChangeKind::Reset, kInvalidTreeNodeId, kInvalidTreeNodeId, 0, count});
}

void TreeModel::notify(TreeChange change) {
    ++revision_;
    if (revision_ == 0)
        revision_ = 1;
    changed_.emit(*this, change);
}

bool TreeExpansionModel::expanded(TreeNodeId id) const {
    return id != kInvalidTreeNodeId && expanded_.find(id) != expanded_.end();
}

bool TreeExpansionModel::set_expanded(TreeNodeId id, bool value) {
    if (id == kInvalidTreeNodeId)
        return false;
    const bool changed = value ? expanded_.insert(id).second : expanded_.erase(id) != 0;
    if (!changed)
        return false;
    ++revision_;
    if (revision_ == 0)
        revision_ = 1;
    changed_.emit(*this, id, value);
    return true;
}

bool TreeExpansionModel::toggle(TreeNodeId id) { return set_expanded(id, !expanded(id)); }

bool TreeExpansionModel::clear() {
    if (expanded_.empty())
        return false;
    expanded_.clear();
    ++revision_;
    if (revision_ == 0)
        revision_ = 1;
    changed_.emit(*this, kInvalidTreeNodeId, false);
    return true;
}

bool TreeExpansionModel::reconcile(const TreeModel& model) {
    bool changed = false;
    for (auto it = expanded_.begin(); it != expanded_.end();) {
        if (!model.contains(*it)) {
            it = expanded_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (!changed)
        return false;
    ++revision_;
    if (revision_ == 0)
        revision_ = 1;
    changed_.emit(*this, kInvalidTreeNodeId, false);
    return true;
}

} // namespace termin::gui_native
