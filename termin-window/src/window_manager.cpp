#include "termin/window/window_manager.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

extern "C" {
#include <tcbase/tc_log.h>
}

namespace termin {

namespace {

[[noreturn]] void manager_logic_error(const std::string& message) {
    tc_log_error("[WindowManager] %s", message.c_str());
    throw std::logic_error(message);
}

[[noreturn]] void manager_invalid_argument(const std::string& message) {
    tc_log_error("[WindowManager] %s", message.c_str());
    throw std::invalid_argument(message);
}

uint32_t next_generation(uint32_t generation) {
    ++generation;
    return generation == 0 ? 1 : generation;
}

} // namespace

struct WindowManager::Impl {
    struct Slot {
        uint32_t generation = 1;
        BackendWindowPtr window;
        std::deque<WindowEvent> pending_events;
    };

    WindowedGraphicsSession* session = nullptr;
    std::thread::id owner_thread = std::this_thread::get_id();
    std::vector<Slot> slots;
    std::vector<uint32_t> free_slots;
    std::vector<WindowHandle> creation_order;
    bool open = true;

    explicit Impl(WindowedGraphicsSession& session_ref)
        : session(&session_ref) {
        session->attach_window_manager();
    }

    void require_owner(const char* operation) const {
        if (std::this_thread::get_id() != owner_thread) {
            manager_logic_error(
                std::string("WindowManager::") + operation +
                " requires the owner thread");
        }
    }

    void require_open(const char* operation) const {
        require_owner(operation);
        if (!open || !session) {
            manager_logic_error(
                std::string("WindowManager::") + operation +
                " called after close");
        }
        if (session->is_closed()) {
            manager_logic_error(
                std::string("WindowManager::") + operation +
                " called after WindowedGraphicsSession close");
        }
    }

    Slot* find(WindowHandle handle) {
        if (!handle || handle.slot > slots.size()) return nullptr;
        Slot& slot = slots[handle.slot - 1];
        if (!slot.window || slot.generation != handle.generation) return nullptr;
        return &slot;
    }

    const Slot* find(WindowHandle handle) const {
        if (!handle || handle.slot > slots.size()) return nullptr;
        const Slot& slot = slots[handle.slot - 1];
        if (!slot.window || slot.generation != handle.generation) return nullptr;
        return &slot;
    }

    Slot& require_slot(WindowHandle handle, const char* operation) {
        Slot* slot = find(handle);
        if (!slot) {
            manager_invalid_argument(
                std::string("WindowManager::") + operation +
                " received an invalid or stale WindowHandle");
        }
        return *slot;
    }

    const Slot& require_slot(WindowHandle handle, const char* operation) const {
        const Slot* slot = find(handle);
        if (!slot) {
            manager_invalid_argument(
                std::string("WindowManager::") + operation +
                " received an invalid or stale WindowHandle");
        }
        return *slot;
    }

    void destroy(WindowHandle handle) {
        Slot& slot = require_slot(handle, "destroy_window");
        slot.window->close();
        slot.window.reset();
        slot.pending_events.clear();
        slot.generation = next_generation(slot.generation);
        free_slots.push_back(handle.slot - 1);
        std::erase(creation_order, handle);
    }
};

WindowManager::WindowManager(WindowedGraphicsSession& session)
    : impl_(std::make_unique<Impl>(session)) {}

WindowManager::~WindowManager() {
    if (!impl_ || !impl_->open) return;
    try {
        close();
    } catch (const std::exception& error) {
        tc_log_error("[WindowManager] shutdown failed: %s", error.what());
    } catch (...) {
        tc_log_error("[WindowManager] shutdown failed with an unknown exception");
    }
}

WindowHandle WindowManager::create_window(const WindowConfig& config) {
    impl_->require_open("create_window");
    BackendWindowPtr created = impl_->session->create_window(config);
    if (!created) {
        manager_logic_error(
            "WindowManager::create_window received a null BackendWindow");
    }
    if (&created->graphics_host() != &impl_->session->graphics()) {
        created->close();
        manager_logic_error(
            "WindowManager::create_window received a window from another GraphicsHost");
    }

    uint32_t slot_index = 0;
    if (!impl_->free_slots.empty()) {
        slot_index = impl_->free_slots.back();
        impl_->free_slots.pop_back();
    } else {
        if (impl_->slots.size() >= std::numeric_limits<uint32_t>::max()) {
            created->close();
            manager_logic_error("WindowManager exhausted WindowHandle slots");
        }
        slot_index = static_cast<uint32_t>(impl_->slots.size());
        impl_->slots.emplace_back();
    }

    Impl::Slot& slot = impl_->slots[slot_index];
    slot.window = std::move(created);
    WindowHandle handle{slot_index + 1, slot.generation};
    impl_->creation_order.push_back(handle);
    return handle;
}

void WindowManager::destroy_window(WindowHandle handle) {
    impl_->require_open("destroy_window");
    impl_->destroy(handle);
}

bool WindowManager::contains(WindowHandle handle) const {
    impl_->require_open("contains");
    return impl_->find(handle) != nullptr;
}

size_t WindowManager::size() const {
    impl_->require_open("size");
    return impl_->creation_order.size();
}

std::vector<WindowHandle> WindowManager::handles() const {
    impl_->require_open("handles");
    return impl_->creation_order;
}

BackendWindow& WindowManager::window(WindowHandle handle) {
    impl_->require_open("window");
    return *impl_->require_slot(handle, "window").window;
}

const BackendWindow& WindowManager::window(WindowHandle handle) const {
    impl_->require_open("window");
    return *impl_->require_slot(handle, "window").window;
}

size_t WindowManager::pump_events() {
    impl_->require_open("pump_events");
    size_t pumped = 0;
    const std::vector<WindowHandle> snapshot = impl_->creation_order;
    for (WindowHandle handle : snapshot) {
        Impl::Slot* slot = impl_->find(handle);
        if (!slot) continue;
        WindowEvent event;
        while (slot->window->poll_event(event)) {
            slot->pending_events.push_back(std::move(event));
            ++pumped;
            event = {};
        }
    }
    return pumped;
}

std::vector<WindowEvent> WindowManager::take_events(WindowHandle handle) {
    impl_->require_open("take_events");
    Impl::Slot& slot = impl_->require_slot(handle, "take_events");
    std::vector<WindowEvent> events;
    events.reserve(slot.pending_events.size());
    while (!slot.pending_events.empty()) {
        events.push_back(std::move(slot.pending_events.front()));
        slot.pending_events.pop_front();
    }
    return events;
}

size_t WindowManager::pending_event_count(WindowHandle handle) const {
    impl_->require_open("pending_event_count");
    return impl_->require_slot(handle, "pending_event_count").pending_events.size();
}

void WindowManager::close() {
    if (!impl_ || !impl_->open) return;
    impl_->require_owner("close");

    while (!impl_->creation_order.empty()) {
        impl_->destroy(impl_->creation_order.back());
    }
    impl_->session->detach_window_manager();
    impl_->session = nullptr;
    impl_->open = false;
}

bool WindowManager::is_open() const noexcept {
    return impl_ && impl_->open;
}

} // namespace termin
