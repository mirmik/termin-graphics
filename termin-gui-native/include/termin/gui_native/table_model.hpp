#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {

using TableRowId = uint64_t;
inline constexpr TableRowId kInvalidTableRowId = 0;

struct TableRowData {
    std::string stable_id;
    std::vector<std::string> cells;
    bool enabled = true;
};

struct TableRow {
    TableRowId id = kInvalidTableRowId;
    TableRowData data;
};

enum class TableChangeKind { Reset, Insert, Update, Erase };

struct TableChange {
    TableChangeKind kind = TableChangeKind::Reset;
    size_t index = 0;
    size_t count = 0;
    TableRowId row = kInvalidTableRowId;
};

class TableModel {
  private:
    std::vector<TableRow> rows_;
    std::unordered_map<TableRowId, size_t> indices_;
    TableRowId next_id_ = 1;
    uint64_t revision_ = 1;
    Signal<TableModel&, const TableChange&> changed_;

  public:
    size_t size() const { return rows_.size(); }
    bool empty() const { return rows_.empty(); }
    uint64_t revision() const { return revision_; }
    bool contains(TableRowId id) const;
    size_t index_of(TableRowId id) const;
    const TableRow& row_at(size_t index) const;
    const TableRow& row(TableRowId id) const;
    const std::vector<TableRow>& rows() const { return rows_; }
    Signal<TableModel&, const TableChange&>& changed() { return changed_; }

    void set_rows(std::vector<TableRowData> rows);
    TableRowId append(TableRowData row);
    TableRowId insert(size_t index, TableRowData row);
    void update(TableRowId id, TableRowData row);
    void erase(TableRowId id);
    void clear();

  private:
    static void validate_row(const TableRowData& row);
    void rebuild_indices(size_t first = 0);
    void notify(TableChange change);

};

enum class TableColumnPolicy { Fixed, Stretch };

struct TableColumn {
    std::string stable_id;
    std::string header;
    TableColumnPolicy policy = TableColumnPolicy::Stretch;
    float width = 0.0f;
    float min_width = 40.0f;
    float max_width = 0.0f;
    float stretch = 1.0f;
    bool resizable = true;
};

enum class TableColumnChangeKind { Reset, Insert, Update, Erase };

struct TableColumnChange {
    TableColumnChangeKind kind = TableColumnChangeKind::Reset;
    size_t index = 0;
    size_t count = 0;
};

class TableColumnModel {
  private:
    std::vector<TableColumn> columns_;
    uint64_t revision_ = 1;
    Signal<TableColumnModel&, const TableColumnChange&> changed_;

  public:
    size_t size() const { return columns_.size(); }
    bool empty() const { return columns_.empty(); }
    uint64_t revision() const { return revision_; }
    const TableColumn& column(size_t index) const;
    const std::vector<TableColumn>& columns() const { return columns_; }
    Signal<TableColumnModel&, const TableColumnChange&>& changed() { return changed_; }

    void set_columns(std::vector<TableColumn> columns);
    void append(TableColumn column);
    void insert(size_t index, TableColumn column);
    void update(size_t index, TableColumn column);
    float resize(size_t index, float width);
    void erase(size_t index);
    void clear();

  private:
    static void validate_column(const TableColumn& column);
    static void validate_unique_ids(const std::vector<TableColumn>& columns);
    void notify(TableColumnChange change);

};

} // namespace termin::gui_native
