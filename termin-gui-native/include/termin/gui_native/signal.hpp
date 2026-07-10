#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace termin::gui_native {

template<typename... Args>
class Signal {
public:
    using Callback = std::function<void(Args...)>;
    size_t connect(Callback callback) { if (!callback) return 0; const size_t id = next_id_++; slots_.push_back(Slot {id, std::move(callback)}); return id; }
    bool disconnect(size_t id) {
        if (id == 0) return false;
        const auto before = slots_.size();
        slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [id](const Slot& slot) { return slot.id == id; }), slots_.end());
        return slots_.size() != before;
    }
    void emit(Args... args) const { const std::vector<Slot> snapshot = slots_; for (const Slot& slot : snapshot) slot.callback(args...); }
    size_t size() const { return slots_.size(); }
private:
    struct Slot { size_t id = 0; Callback callback; };
    size_t next_id_ = 1;
    std::vector<Slot> slots_;
};

} // namespace termin::gui_native
