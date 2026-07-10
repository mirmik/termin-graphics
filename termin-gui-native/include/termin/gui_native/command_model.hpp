#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <termin/gui_native/signal.hpp>

namespace termin::gui_native {

using CommandId = uint64_t;
inline constexpr CommandId kInvalidCommandId = 0;

class CommandModel;

enum class CommandKind { Action, Separator };

struct CommandData {
    std::string stable_id;
    std::string label;
    std::string icon;
    std::string shortcut;
    std::string tooltip;
    CommandKind kind = CommandKind::Action;
    bool enabled = true;
    bool checkable = false;
    bool checked = false;
    uint32_t texture_id = 0;
    std::shared_ptr<CommandModel> submenu;
};

struct Command {
    CommandId id = kInvalidCommandId;
    CommandData data;
};

enum class CommandChangeKind { Reset, Insert, Update, Erase };

struct CommandChange {
    CommandChangeKind kind = CommandChangeKind::Reset;
    size_t index = 0;
    size_t count = 0;
    CommandId command = kInvalidCommandId;
};

class CommandModel {
  public:
    size_t size() const { return commands_.size(); }
    bool empty() const { return commands_.empty(); }
    uint64_t revision() const { return revision_; }
    bool contains(CommandId id) const;
    size_t index_of(CommandId id) const;
    const Command& command_at(size_t index) const;
    const Command& command(CommandId id) const;
    const std::vector<Command>& commands() const { return commands_; }
    Signal<CommandModel&, const CommandChange&>& changed() { return changed_; }

    void set_commands(std::vector<CommandData> commands);
    CommandId append(CommandData command);
    CommandId insert(size_t index, CommandData command);
    void update(CommandId id, CommandData command);
    void set_enabled(CommandId id, bool enabled);
    void set_checked(CommandId id, bool checked);
    void erase(CommandId id);
    void clear();

  private:
    static void validate_command(const CommandData& command);
    static void validate_stable_ids(const std::vector<CommandData>& commands);
    void rebuild_indices(size_t first = 0);
    void notify(CommandChange change);

    std::vector<Command> commands_;
    std::unordered_map<CommandId, size_t> indices_;
    CommandId next_id_ = 1;
    uint64_t revision_ = 1;
    Signal<CommandModel&, const CommandChange&> changed_;
};

} // namespace termin::gui_native
