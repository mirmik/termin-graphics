#include "widgets_internal.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace termin::gui_native {
using namespace detail;

bool TableModel::contains(TableRowId id) const {
    return id != kInvalidTableRowId && indices_.find(id) != indices_.end();
}

size_t TableModel::index_of(TableRowId id) const {
    const auto found = indices_.find(id);
    return found == indices_.end() ? SIZE_MAX : found->second;
}

const TableRow& TableModel::row_at(size_t index) const {
    if (index >= rows_.size())
        throw std::out_of_range("table row index out of range");
    return rows_[index];
}

const TableRow& TableModel::row(TableRowId id) const {
    const size_t index = index_of(id);
    if (index == SIZE_MAX)
        throw std::out_of_range("table row id is not live");
    return rows_[index];
}

void TableModel::validate_row(const TableRowData& row) {
    if (!valid_utf8(row.stable_id)) {
        tc_log_error("[termin-gui-native] table model rejected invalid UTF-8 stable id");
        throw std::invalid_argument("table row stable id must be valid UTF-8");
    }
    for (const std::string& cell : row.cells) {
        if (!valid_utf8(cell)) {
            tc_log_error("[termin-gui-native] table model rejected invalid UTF-8 cell");
            throw std::invalid_argument("table cells must be valid UTF-8");
        }
    }
}

void TableModel::rebuild_indices(size_t first) {
    for (size_t index = first; index < rows_.size(); ++index)
        indices_[rows_[index].id] = index;
}

void TableModel::set_rows(std::vector<TableRowData> rows) {
    for (const TableRowData& row : rows)
        validate_row(row);
    std::vector<TableRow> next;
    next.reserve(rows.size());
    for (TableRowData& row : rows) {
        if (next_id_ == kInvalidTableRowId) {
            tc_log_error("[termin-gui-native] table row id space exhausted");
            throw std::overflow_error("table row id space exhausted");
        }
        next.push_back(TableRow{next_id_++, std::move(row)});
    }
    rows_ = std::move(next);
    indices_.clear();
    rebuild_indices();
    notify(TableChange{TableChangeKind::Reset, 0, rows_.size(), kInvalidTableRowId});
}

TableRowId TableModel::append(TableRowData row) { return insert(rows_.size(), std::move(row)); }

TableRowId TableModel::insert(size_t index, TableRowData row) {
    validate_row(row);
    if (index > rows_.size())
        throw std::out_of_range("table row insertion index out of range");
    if (next_id_ == kInvalidTableRowId) {
        tc_log_error("[termin-gui-native] table row id space exhausted");
        throw std::overflow_error("table row id space exhausted");
    }
    const TableRowId id = next_id_++;
    rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(index), TableRow{id, std::move(row)});
    rebuild_indices(index);
    notify(TableChange{TableChangeKind::Insert, index, 1, id});
    return id;
}

void TableModel::update(TableRowId id, TableRowData row) {
    validate_row(row);
    const size_t index = index_of(id);
    if (index == SIZE_MAX)
        throw std::out_of_range("table row id is not live");
    rows_[index].data = std::move(row);
    notify(TableChange{TableChangeKind::Update, index, 1, id});
}

void TableModel::erase(TableRowId id) {
    const size_t index = index_of(id);
    if (index == SIZE_MAX)
        throw std::out_of_range("table row id is not live");
    rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(index));
    indices_.erase(id);
    rebuild_indices(index);
    notify(TableChange{TableChangeKind::Erase, index, 1, id});
}

void TableModel::clear() {
    if (rows_.empty())
        return;
    const size_t count = rows_.size();
    rows_.clear();
    indices_.clear();
    notify(TableChange{TableChangeKind::Reset, 0, count, kInvalidTableRowId});
}

void TableModel::notify(TableChange change) {
    ++revision_;
    if (revision_ == 0)
        revision_ = 1;
    changed_.emit(*this, change);
}

const TableColumn& TableColumnModel::column(size_t index) const {
    if (index >= columns_.size())
        throw std::out_of_range("table column index out of range");
    return columns_[index];
}

void TableColumnModel::validate_column(const TableColumn& column) {
    if (column.stable_id.empty() || !valid_utf8(column.stable_id) || !valid_utf8(column.header)) {
        tc_log_error("[termin-gui-native] table column rejected invalid id/header");
        throw std::invalid_argument("table column id must be non-empty and strings valid UTF-8");
    }
    if (!std::isfinite(column.width) || column.width < 0.0f || !std::isfinite(column.min_width) ||
        column.min_width < 0.0f || !std::isfinite(column.max_width) || column.max_width < 0.0f ||
        (column.max_width > 0.0f && column.max_width < column.min_width) ||
        !std::isfinite(column.stretch) || column.stretch <= 0.0f ||
        (column.policy == TableColumnPolicy::Fixed && column.width <= 0.0f)) {
        tc_log_error("[termin-gui-native] table column rejected invalid sizing metrics");
        throw std::invalid_argument("invalid table column sizing metrics");
    }
}

void TableColumnModel::validate_unique_ids(const std::vector<TableColumn>& columns) {
    std::unordered_set<std::string> ids;
    for (const TableColumn& column : columns) {
        validate_column(column);
        if (!ids.insert(column.stable_id).second) {
            tc_log_error("[termin-gui-native] table columns require unique stable ids");
            throw std::invalid_argument("duplicate table column stable id");
        }
    }
}

void TableColumnModel::set_columns(std::vector<TableColumn> columns) {
    validate_unique_ids(columns);
    columns_ = std::move(columns);
    notify(TableColumnChange{TableColumnChangeKind::Reset, 0, columns_.size()});
}

void TableColumnModel::append(TableColumn column) { insert(columns_.size(), std::move(column)); }

void TableColumnModel::insert(size_t index, TableColumn column) {
    validate_column(column);
    if (index > columns_.size())
        throw std::out_of_range("table column insertion index out of range");
    if (std::any_of(columns_.begin(), columns_.end(), [&column](const TableColumn& existing) {
            return existing.stable_id == column.stable_id;
        })) {
        tc_log_error("[termin-gui-native] table columns require unique stable ids");
        throw std::invalid_argument("duplicate table column stable id");
    }
    columns_.insert(columns_.begin() + static_cast<std::ptrdiff_t>(index), std::move(column));
    notify(TableColumnChange{TableColumnChangeKind::Insert, index, 1});
}

void TableColumnModel::update(size_t index, TableColumn column) {
    validate_column(column);
    if (index >= columns_.size())
        throw std::out_of_range("table column index out of range");
    for (size_t other = 0; other < columns_.size(); ++other) {
        if (other != index && columns_[other].stable_id == column.stable_id) {
            tc_log_error("[termin-gui-native] table columns require unique stable ids");
            throw std::invalid_argument("duplicate table column stable id");
        }
    }
    columns_[index] = std::move(column);
    notify(TableColumnChange{TableColumnChangeKind::Update, index, 1});
}

float TableColumnModel::resize(size_t index, float width) {
    if (index >= columns_.size())
        throw std::out_of_range("table column index out of range");
    if (!std::isfinite(width)) {
        tc_log_error("[termin-gui-native] table column rejected non-finite resize width");
        throw std::invalid_argument("table column width must be finite");
    }
    TableColumn& column = columns_[index];
    if (!column.resizable)
        return column.width;
    width = std::max(column.min_width, width);
    if (column.max_width > 0.0f)
        width = std::min(column.max_width, width);
    if (column.policy == TableColumnPolicy::Fixed && column.width == width)
        return width;
    column.policy = TableColumnPolicy::Fixed;
    column.width = width;
    notify(TableColumnChange{TableColumnChangeKind::Update, index, 1});
    return width;
}

void TableColumnModel::erase(size_t index) {
    if (index >= columns_.size())
        throw std::out_of_range("table column index out of range");
    columns_.erase(columns_.begin() + static_cast<std::ptrdiff_t>(index));
    notify(TableColumnChange{TableColumnChangeKind::Erase, index, 1});
}

void TableColumnModel::clear() {
    if (columns_.empty())
        return;
    const size_t count = columns_.size();
    columns_.clear();
    notify(TableColumnChange{TableColumnChangeKind::Reset, 0, count});
}

void TableColumnModel::notify(TableColumnChange change) {
    ++revision_;
    if (revision_ == 0)
        revision_ = 1;
    changed_.emit(*this, change);
}

} // namespace termin::gui_native
