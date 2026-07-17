#pragma once

#include <cstddef>
#include <vector>

#include <termin/gui_native/tree_model.hpp>

namespace termin::gui_native {

struct TreeVisibleRow {
  TreeNodeId node = kInvalidTreeNodeId;
  size_t depth = 0;
};

template <typename Roots, typename Children, typename Expanded>
std::vector<TreeVisibleRow>
build_tree_projection(const Roots &roots, Children &&children,
                      Expanded &&expanded, size_t reserve = 0) {
  std::vector<TreeVisibleRow> visible;
  visible.reserve(reserve);
  const auto append = [&](const auto &self, TreeNodeId node,
                          size_t depth) -> void {
    visible.push_back(TreeVisibleRow{node, depth});
    if (!expanded(node))
      return;
    for (TreeNodeId child : children(node))
      self(self, child, depth + 1);
  };
  for (TreeNodeId root : roots)
    append(append, root, 0);
  return visible;
}

} // namespace termin::gui_native
