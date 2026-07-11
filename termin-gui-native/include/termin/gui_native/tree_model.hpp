#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <termin/gui_native/collection_model.hpp>
#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {

using TreeNodeId = uint64_t;
inline constexpr TreeNodeId kInvalidTreeNodeId = 0;

struct TreeNode {
    TreeNodeId id = kInvalidTreeNodeId;
    CollectionItem item;
    TreeNodeId parent = kInvalidTreeNodeId;
    std::vector<TreeNodeId> children;
};

enum class TreeChangeKind { Reset, Insert, Update, Erase, Move };

struct TreeChange {
    TreeChangeKind kind = TreeChangeKind::Reset;
    TreeNodeId node = kInvalidTreeNodeId;
    TreeNodeId parent = kInvalidTreeNodeId;
    size_t index = 0;
    size_t count = 0;
};

class TreeModel {
  private:
    std::unordered_map<TreeNodeId, TreeNode> nodes_;
    std::vector<TreeNodeId> roots_;
    TreeNodeId next_id_ = 1;
    uint64_t revision_ = 1;
    Signal<TreeModel&, const TreeChange&> changed_;

  public:
    size_t size() const { return nodes_.size(); }
    bool empty() const { return nodes_.empty(); }
    uint64_t revision() const { return revision_; }
    bool contains(TreeNodeId id) const;
    const TreeNode& node(TreeNodeId id) const;
    const std::vector<TreeNodeId>& roots() const { return roots_; }
    const std::vector<TreeNodeId>& children(TreeNodeId parent) const;
    Signal<TreeModel&, const TreeChange&>& changed() { return changed_; }

    TreeNodeId append_root(CollectionItem item);
    TreeNodeId append_child(TreeNodeId parent, CollectionItem item);
    TreeNodeId insert_child(TreeNodeId parent, size_t index, CollectionItem item);
    void update(TreeNodeId id, CollectionItem item);
    void move(TreeNodeId id, TreeNodeId new_parent, size_t index = SIZE_MAX);
    void erase(TreeNodeId id);
    void clear();

  private:
    static void validate_item(const CollectionItem& item);
    std::vector<TreeNodeId>& sibling_list(TreeNodeId parent);
    const std::vector<TreeNodeId>& sibling_list(TreeNodeId parent) const;
    bool is_descendant(TreeNodeId node, TreeNodeId ancestor) const;
    void erase_subtree(TreeNodeId id, size_t& count);
    void notify(TreeChange change);

};

class TreeExpansionModel {
  private:
    std::unordered_set<TreeNodeId> expanded_;
    uint64_t revision_ = 1;
    Signal<TreeExpansionModel&, TreeNodeId, bool> changed_;

  public:
    uint64_t revision() const { return revision_; }
    bool expanded(TreeNodeId id) const;
    bool set_expanded(TreeNodeId id, bool expanded);
    bool toggle(TreeNodeId id);
    bool clear();
    bool reconcile(const TreeModel& model);
    Signal<TreeExpansionModel&, TreeNodeId, bool>& changed() { return changed_; }

  private:
};

} // namespace termin::gui_native
