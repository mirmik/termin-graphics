// window_manager.hpp - framework-neutral ownership of presentation windows.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "termin/platform/backend_window.hpp"
#include "termin/window/api.h"
#include "termin/window/event.hpp"

namespace termin {

struct WindowHandle {
    uint32_t slot = 0;
    uint32_t generation = 0;

    explicit operator bool() const noexcept {
        return slot != 0 && generation != 0;
    }

    friend bool operator==(WindowHandle, WindowHandle) = default;
};

struct WindowHandleHash {
    size_t operator()(WindowHandle handle) const noexcept {
        const uint64_t packed =
            (static_cast<uint64_t>(handle.generation) << 32u) | handle.slot;
        return std::hash<uint64_t>{}(packed);
    }
};

// Owns a framework-neutral collection of BackendWindow objects created on one
// borrowed WindowedGraphicsSession. Application code owns the mapping from
// WindowHandle to UI/controller/rendering content and all main/exit policy.
class TERMIN_WINDOW_API WindowManager {
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    explicit WindowManager(WindowedGraphicsSession& session);
    ~WindowManager();

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;
    WindowManager(WindowManager&&) = delete;
    WindowManager& operator=(WindowManager&&) = delete;

    WindowHandle create_window(const WindowConfig& config);
    void destroy_window(WindowHandle handle);

    bool contains(WindowHandle handle) const;
    size_t size() const;
    std::vector<WindowHandle> handles() const;

    BackendWindow& window(WindowHandle handle);
    const BackendWindow& window(WindowHandle handle) const;

    // Appends all currently available portable events to per-window queues.
    // Events remain queued until take_events() is called.
    size_t pump_events();
    std::vector<WindowEvent> take_events(WindowHandle handle);
    size_t pending_event_count(WindowHandle handle) const;

    // Closes all windows in reverse creation order and detaches from the
    // borrowed session. Idempotent.
    void close();
    bool is_open() const noexcept;
};

} // namespace termin
