#include "widgets_internal.hpp"

#include <stdexcept>
#include <unordered_set>

namespace termin::gui_native {
using namespace detail;

bool CommandModel::contains(CommandId id) const {
    return id != kInvalidCommandId && indices_.find(id) != indices_.end();
}

size_t CommandModel::index_of(CommandId id) const {
    const auto found = indices_.find(id);
    return found == indices_.end() ? SIZE_MAX : found->second;
}

const Command& CommandModel::command_at(size_t index) const {
    if (index >= commands_.size())
        throw std::out_of_range("command index out of range");
    return commands_[index];
}

const Command& CommandModel::command(CommandId id) const {
    const size_t index = index_of(id);
    if (index == SIZE_MAX)
        throw std::out_of_range("command id is not live");
    return commands_[index];
}

void CommandModel::validate_command(const CommandData& command) {
    if (command.stable_id.empty() || !valid_utf8(command.stable_id) || !valid_utf8(command.label) ||
        !valid_utf8(command.icon) || !valid_utf8(command.shortcut) ||
        !valid_utf8(command.tooltip)) {
        tc_log_error("[termin-gui-native] command model rejected invalid command strings");
        throw std::invalid_argument("command id must be non-empty and strings valid UTF-8");
    }
    if (command.kind == CommandKind::Separator &&
        (command.checkable || command.checked || command.submenu)) {
        tc_log_error("[termin-gui-native] separator command carries action-only state");
        throw std::invalid_argument("separator cannot be checkable, checked, or own a submenu");
    }
    if (command.checked && !command.checkable) {
        tc_log_error("[termin-gui-native] checked command is not checkable");
        throw std::invalid_argument("checked command must be checkable");
    }
}

void CommandModel::validate_stable_ids(const std::vector<CommandData>& commands) {
    std::unordered_set<std::string> ids;
    for (const CommandData& command : commands) {
        validate_command(command);
        if (!ids.insert(command.stable_id).second) {
            tc_log_error("[termin-gui-native] command model requires unique stable ids");
            throw std::invalid_argument("duplicate command stable id");
        }
    }
}

void CommandModel::rebuild_indices(size_t first) {
    for (size_t index = first; index < commands_.size(); ++index) {
        indices_[commands_[index].id] = index;
    }
}

void CommandModel::set_commands(std::vector<CommandData> commands) {
    validate_stable_ids(commands);
    std::vector<Command> next;
    next.reserve(commands.size());
    for (CommandData& command : commands) {
        if (next_id_ == kInvalidCommandId) {
            tc_log_error("[termin-gui-native] command id space exhausted");
            throw std::overflow_error("command id space exhausted");
        }
        next.push_back(Command{next_id_++, std::move(command)});
    }
    commands_ = std::move(next);
    indices_.clear();
    rebuild_indices();
    notify(CommandChange{CommandChangeKind::Reset, 0, commands_.size(), kInvalidCommandId});
}

CommandId CommandModel::append(CommandData command) {
    return insert(commands_.size(), std::move(command));
}

CommandId CommandModel::insert(size_t index, CommandData command) {
    validate_command(command);
    if (index > commands_.size())
        throw std::out_of_range("command insertion index out of range");
    for (const Command& existing : commands_) {
        if (existing.data.stable_id == command.stable_id) {
            tc_log_error("[termin-gui-native] command model requires unique stable ids");
            throw std::invalid_argument("duplicate command stable id");
        }
    }
    if (next_id_ == kInvalidCommandId) {
        tc_log_error("[termin-gui-native] command id space exhausted");
        throw std::overflow_error("command id space exhausted");
    }
    const CommandId id = next_id_++;
    commands_.insert(commands_.begin() + static_cast<std::ptrdiff_t>(index),
                     Command{id, std::move(command)});
    rebuild_indices(index);
    notify(CommandChange{CommandChangeKind::Insert, index, 1, id});
    return id;
}

void CommandModel::update(CommandId id, CommandData command) {
    validate_command(command);
    const size_t index = index_of(id);
    if (index == SIZE_MAX)
        throw std::out_of_range("command id is not live");
    for (size_t other = 0; other < commands_.size(); ++other) {
        if (other != index && commands_[other].data.stable_id == command.stable_id) {
            tc_log_error("[termin-gui-native] command model requires unique stable ids");
            throw std::invalid_argument("duplicate command stable id");
        }
    }
    commands_[index].data = std::move(command);
    notify(CommandChange{CommandChangeKind::Update, index, 1, id});
}

void CommandModel::set_enabled(CommandId id, bool enabled) {
    const size_t index = index_of(id);
    if (index == SIZE_MAX)
        throw std::out_of_range("command id is not live");
    if (commands_[index].data.enabled == enabled)
        return;
    commands_[index].data.enabled = enabled;
    notify(CommandChange{CommandChangeKind::Update, index, 1, id});
}

void CommandModel::set_checked(CommandId id, bool checked) {
    const size_t index = index_of(id);
    if (index == SIZE_MAX)
        throw std::out_of_range("command id is not live");
    if (!commands_[index].data.checkable) {
        tc_log_error("[termin-gui-native] cannot check a non-checkable command");
        throw std::invalid_argument("command is not checkable");
    }
    if (commands_[index].data.checked == checked)
        return;
    commands_[index].data.checked = checked;
    notify(CommandChange{CommandChangeKind::Update, index, 1, id});
}

void CommandModel::erase(CommandId id) {
    const size_t index = index_of(id);
    if (index == SIZE_MAX)
        throw std::out_of_range("command id is not live");
    commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(index));
    indices_.erase(id);
    rebuild_indices(index);
    notify(CommandChange{CommandChangeKind::Erase, index, 1, id});
}

void CommandModel::clear() {
    if (commands_.empty())
        return;
    const size_t count = commands_.size();
    commands_.clear();
    indices_.clear();
    notify(CommandChange{CommandChangeKind::Reset, 0, count, kInvalidCommandId});
}

void CommandModel::notify(CommandChange change) {
    ++revision_;
    if (revision_ == 0)
        revision_ = 1;
    changed_.emit(*this, change);
}

} // namespace termin::gui_native
