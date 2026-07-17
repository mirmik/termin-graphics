#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <termin/gui_native/signal.hpp>
#include <termin/gui_native/tree_model.hpp>

namespace termin::gui_native {

using TreeTableNodeId = TreeNodeId;
inline constexpr TreeTableNodeId kInvalidTreeTableNodeId = kInvalidTreeNodeId;

struct TreeTableRowData {
  std::string stable_id;
  std::string parent_stable_id;
  std::vector<std::string> cells;
  bool enabled = true;
};

struct TreeTableNode {
  TreeTableNodeId id = kInvalidTreeTableNodeId;
  TreeTableRowData data;
  TreeTableNodeId parent = kInvalidTreeTableNodeId;
  std::vector<TreeTableNodeId> children;
};

class TreeTableModel {
private:
  std::unordered_map<TreeTableNodeId, TreeTableNode> nodes_;
  std::unordered_map<std::string, TreeTableNodeId> stable_ids_;
  std::vector<TreeTableNodeId> roots_;
  TreeTableNodeId next_id_ = 1;
  uint64_t revision_ = 1;
  Signal<TreeTableModel &> changed_;

public:
  size_t size() const { return nodes_.size(); }
  bool empty() const { return nodes_.empty(); }
  uint64_t revision() const { return revision_; }
  bool contains(TreeTableNodeId id) const;
  TreeTableNodeId find(const std::string &stable_id) const;
  const TreeTableNode &node(TreeTableNodeId id) const;
  const std::vector<TreeTableNodeId> &roots() const { return roots_; }
  const std::vector<TreeTableNodeId> &children(TreeTableNodeId parent) const;
  Signal<TreeTableModel &> &changed() { return changed_; }

  // Rows may be supplied in any order. Their order defines root and sibling
  // order. Live stable ids retain their numeric node ids across resets.
  void set_rows(std::vector<TreeTableRowData> rows);
  void clear();

private:
  static void validate_row(const TreeTableRowData &row);
  void notify();
};

} // namespace termin::gui_native
