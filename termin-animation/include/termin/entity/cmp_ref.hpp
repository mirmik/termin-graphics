#pragma once

#include "core/tc_entity_pool.h"
#include <termin/entity/component.hpp>

namespace termin {

// Safe reference to a CxxComponent that validates entity liveness before access.
// Stores both the component pointer and the owner entity handle.
// When the entity is destroyed, get() returns nullptr instead of a dangling pointer.
//
// Usage:
//   CmpRef<CameraComponent> camera_ref(camera);
//   if (CameraComponent* cam = camera_ref.get()) {
//       cam->do_something();
//   }
//
// Note: This does NOT check if the component still exists on the entity,
// only if the entity itself is alive. If component removal without entity
// destruction is a concern, additional validation would be needed.
template<typename T>
class CmpRef {
public:
    // Entity handle for liveness check
    tc_entity_handle entity_handle = TC_ENTITY_HANDLE_INVALID;

    // Raw pointer to the component (may be dangling if entity is dead)
    T* ptr = nullptr;

    CmpRef() = default;

    // Construct from a component pointer
    // Extracts entity handle from the component's owner
    explicit CmpRef(T* component) {
        if (component) {
            ptr = component;
            entity_handle = component->c_component()->owner;
        }
    }

    // Construct from entity handle and component pointer
    CmpRef(tc_entity_handle handle, T* component)
        : entity_handle(handle), ptr(component) {}

    // Check if the reference might be valid
    // (entity is alive; component pointer was non-null)
    bool valid() const {
        if (!ptr) return false;
        return tc_entity_handle_valid(entity_handle);
    }

    // Get the component pointer, or nullptr if entity is dead
    T* get() const {
        if (!valid()) return nullptr;
        return ptr;
    }

    // Arrow operator for convenient access
    // WARNING: Caller must check valid() first, or use get() and null-check
    T* operator->() const {
        return get();
    }

    // Dereference operator
    // WARNING: Caller must check valid() first
    T& operator*() const {
        return *get();
    }

    // Bool conversion for if-checks
    explicit operator bool() const {
        return valid();
    }

    // Reset the reference
    void reset() {
        ptr = nullptr;
        entity_handle = TC_ENTITY_HANDLE_INVALID;
    }

    // Reset with a new component
    void reset(T* component) {
        if (component) {
            ptr = component;
            entity_handle = component->c_component()->owner;
        } else {
            reset();
        }
    }

    // Get the raw pointer without validation (use with caution)
    T* raw() const {
        return ptr;
    }

    // Get the stored entity handle
    tc_entity_handle handle() const {
        return entity_handle;
    }

    // Equality comparison
    bool operator==(const CmpRef& other) const {
        return ptr == other.ptr &&
               tc_entity_handle_eq(entity_handle, other.entity_handle);
    }

    bool operator!=(const CmpRef& other) const {
        return !(*this == other);
    }

    // Comparison with nullptr
    bool operator==(std::nullptr_t) const {
        return ptr == nullptr;
    }

    bool operator!=(std::nullptr_t) const {
        return ptr != nullptr;
    }
};

} // namespace termin
