#pragma once

#include <cstdint>
#include <utility>

namespace termin::gui_native {

// Platform click counts describe a pointer sequence in the host window.  A
// collection activation additionally requires consecutive clicks on the same
// logical item, which only the widget can identify.
template <typename Target>
class LogicalClickTracker {
  private:
    Target invalid_;
    Target previous_;

  public:
    explicit LogicalClickTracker(Target invalid)
        : invalid_(std::move(invalid)), previous_(invalid_) {}

    bool press(const Target& target, uint32_t host_click_count) {
        const bool activated = target != invalid_ && host_click_count >= 2 && previous_ == target;
        previous_ = activated ? invalid_ : target;
        return activated;
    }

    void clear() { previous_ = invalid_; }
};

} // namespace termin::gui_native
