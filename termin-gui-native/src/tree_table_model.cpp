#include "widgets_internal.hpp"

#include <functional>
#include <stdexcept>
#include <unordered_set>

namespace termin::gui_native {
using namespace detail;

bool TreeTableModel::contains(TreeTableNodeId id) const {
  return id != kInvalidTreeTableNodeId && nodes_.find(id) != nodes_.end();
}

TreeTableNodeId TreeTableModel::find(const std::string &stable_id) const {
  const auto found = stable_ids_.find(stable_id);
  return found == stable_ids_.end() ? kInvalidTreeTableNodeId : found->second;
}

const TreeTableNode &TreeTableModel::node(TreeTableNodeId id) const {
  const auto found = nodes_.find(id);
  if (found == nodes_.end())
    throw std::out_of_range("tree-table node id is not live");
  return found->second;
}

const std::vector<TreeTableNodeId> &
TreeTableModel::children(TreeTableNodeId parent) const {
  if (parent == kInvalidTreeTableNodeId)
    return roots_;
  return node(parent).children;
}

void TreeTableModel::validate_row(const TreeTableRowData &row) {
  if (row.stable_id.empty() || !valid_utf8(row.stable_id) ||
      !valid_utf8(row.parent_stable_id)) {
    tc_log_error(
        "[termin-gui-native] tree-table model rejected invalid stable id");
    throw std::invalid_argument(
        "tree-table stable ids must be non-empty valid UTF-8 strings");
  }
  for (const std::string &cell : row.cells) {
    if (!valid_utf8(cell)) {
      tc_log_error(
          "[termin-gui-native] tree-table model rejected invalid UTF-8 cell");
      throw std::invalid_argument("tree-table cells must be valid UTF-8");
    }
  }
}

void TreeTableModel::set_rows(std::vector<TreeTableRowData> rows) {
  std::unordered_map<std::string, size_t> input_indices;
  input_indices.reserve(rows.size());
  for (size_t index = 0; index < rows.size(); ++index) {
    validate_row(rows[index]);
    if (!input_indices.emplace(rows[index].stable_id, index).second) {
      tc_log_error(
          "[termin-gui-native] tree-table model rejected duplicate stable id");
      throw std::invalid_argument("duplicate tree-table stable id");
    }
  }
  for (const TreeTableRowData &row : rows) {
    if (!row.parent_stable_id.empty() &&
        input_indices.find(row.parent_stable_id) == input_indices.end()) {
      tc_log_error(
          "[termin-gui-native] tree-table model rejected missing parent");
      throw std::invalid_argument("tree-table parent stable id is not present");
    }
    if (row.parent_stable_id == row.stable_id) {
      tc_log_error(
          "[termin-gui-native] tree-table model rejected self-parent cycle");
      throw std::invalid_argument("tree-table node cannot parent itself");
    }
  }

  enum class Visit : uint8_t { Unseen, Visiting, Done };
  std::vector<Visit> visits(rows.size(), Visit::Unseen);
  std::function<void(size_t)> visit = [&](size_t index) {
    if (visits[index] == Visit::Done)
      return;
    if (visits[index] == Visit::Visiting) {
      tc_log_error(
          "[termin-gui-native] tree-table model rejected parent cycle");
      throw std::invalid_argument("tree-table parent cycle");
    }
    visits[index] = Visit::Visiting;
    const std::string &parent = rows[index].parent_stable_id;
    if (!parent.empty())
      visit(input_indices.at(parent));
    visits[index] = Visit::Done;
  };
  for (size_t index = 0; index < rows.size(); ++index)
    visit(index);

  std::unordered_map<std::string, TreeTableNodeId> next_stable_ids;
  std::unordered_map<TreeTableNodeId, TreeTableNode> next_nodes;
  std::vector<TreeTableNodeId> next_roots;
  next_stable_ids.reserve(rows.size());
  next_nodes.reserve(rows.size());

  for (TreeTableRowData &row : rows) {
    TreeTableNodeId id = find(row.stable_id);
    if (id == kInvalidTreeTableNodeId) {
      if (next_id_ == kInvalidTreeTableNodeId) {
        tc_log_error("[termin-gui-native] tree-table node id space exhausted");
        throw std::overflow_error("tree-table node id space exhausted");
      }
      id = next_id_++;
    }
    next_stable_ids.emplace(row.stable_id, id);
    next_nodes.emplace(
        id, TreeTableNode{id, std::move(row), kInvalidTreeTableNodeId, {}});
  }

  for (const auto &[stable_id, index] : input_indices) {
    (void)index;
    const TreeTableNodeId id = next_stable_ids.at(stable_id);
    TreeTableNode &current = next_nodes.at(id);
    if (!current.data.parent_stable_id.empty())
      current.parent = next_stable_ids.at(current.data.parent_stable_id);
  }
  // Preserve the input order for both roots and siblings. The row payloads were
  // moved into next_nodes, so invert the still-intact id-to-index lookup.
  std::vector<std::string> ordered_ids(rows.size());
  for (const auto &[stable_id, index] : input_indices)
    ordered_ids[index] = stable_id;
  for (const std::string &stable_id : ordered_ids) {
    const TreeTableNodeId id = next_stable_ids.at(stable_id);
    TreeTableNode &current = next_nodes.at(id);
    if (current.parent == kInvalidTreeTableNodeId)
      next_roots.push_back(id);
    else
      next_nodes.at(current.parent).children.push_back(id);
  }

  nodes_ = std::move(next_nodes);
  stable_ids_ = std::move(next_stable_ids);
  roots_ = std::move(next_roots);
  notify();
}

void TreeTableModel::clear() {
  if (nodes_.empty())
    return;
  nodes_.clear();
  stable_ids_.clear();
  roots_.clear();
  notify();
}

void TreeTableModel::notify() {
  ++revision_;
  if (revision_ == 0)
    revision_ = 1;
  changed_.emit(*this);
}

} // namespace termin::gui_native
