#pragma once

// TcSkeleton - RAII wrapper with handle-based access to tc_skeleton
// Uses tc_skeleton_handle with generation checking for safety

extern "C" {
#include "resources/tc_skeleton.h"
#include "resources/tc_skeleton_registry.h"
}

#include <string>
#include <tcbase/tgfx_intern_string.h>

namespace termin {

// TcSkeleton - skeleton data wrapper with registry integration
// Stores handle (index + generation) instead of raw pointer
class TcSkeleton {
public:
    tc_skeleton_handle handle = tc_skeleton_handle_invalid();

    TcSkeleton() = default;

    explicit TcSkeleton(tc_skeleton_handle h) : handle(h) {
        if (tc_skeleton* s = tc_skeleton_get(handle)) {
            tc_skeleton_add_ref(s);
        }
    }

    TcSkeleton(const TcSkeleton& other) : handle(other.handle) {
        if (tc_skeleton* s = tc_skeleton_get(handle)) {
            tc_skeleton_add_ref(s);
        }
    }

    TcSkeleton(TcSkeleton&& other) noexcept : handle(other.handle) {
        other.handle = tc_skeleton_handle_invalid();
    }

    TcSkeleton& operator=(const TcSkeleton& other) {
        if (this != &other) {
            if (tc_skeleton* s = tc_skeleton_get(handle)) {
                tc_skeleton_release(s);
            }
            handle = other.handle;
            if (tc_skeleton* s = tc_skeleton_get(handle)) {
                tc_skeleton_add_ref(s);
            }
        }
        return *this;
    }

    TcSkeleton& operator=(TcSkeleton&& other) noexcept {
        if (this != &other) {
            if (tc_skeleton* s = tc_skeleton_get(handle)) {
                tc_skeleton_release(s);
            }
            handle = other.handle;
            other.handle = tc_skeleton_handle_invalid();
        }
        return *this;
    }

    ~TcSkeleton() {
        if (tc_skeleton* s = tc_skeleton_get(handle)) {
            tc_skeleton_release(s);
        }
        handle = tc_skeleton_handle_invalid();
    }

    // Get raw C struct pointer
    tc_skeleton* get() const { return tc_skeleton_get(handle); }

    // Query
    bool is_valid() const { return tc_skeleton_is_valid(handle); }

    const char* uuid() const {
        tc_skeleton* s = get();
        return s ? s->header.uuid : "";
    }

    const char* name() const {
        tc_skeleton* s = get();
        return (s && s->header.name) ? s->header.name : "";
    }

    uint32_t version() const {
        tc_skeleton* s = get();
        return s ? s->header.version : 0;
    }

    size_t bone_count() const {
        tc_skeleton* s = get();
        return s ? s->bone_count : 0;
    }

    // Bone access
    tc_bone* bones() const {
        tc_skeleton* s = get();
        return s ? s->bones : nullptr;
    }

    tc_bone* get_bone(size_t index) const {
        tc_skeleton* s = get();
        return s ? tc_skeleton_get_bone(s, index) : nullptr;
    }

    int find_bone(const char* bone_name) const {
        tc_skeleton* s = get();
        return s ? tc_skeleton_find_bone(s, bone_name) : -1;
    }

    // Root bones
    const int32_t* root_indices() const {
        tc_skeleton* s = get();
        return s ? s->root_indices : nullptr;
    }

    size_t root_count() const {
        tc_skeleton* s = get();
        return s ? s->root_count : 0;
    }

    void bump_version() {
        if (tc_skeleton* s = get()) {
            s->header.version++;
        }
    }

    // Trigger lazy load
    bool ensure_loaded() {
        return tc_skeleton_ensure_loaded(handle);
    }

    // Allocate bones
    tc_bone* alloc_bones(size_t count) {
        tc_skeleton* s = get();
        return s ? tc_skeleton_alloc_bones(s, count) : nullptr;
    }

    // Rebuild root indices after modifying bones
    void rebuild_roots() {
        if (tc_skeleton* s = get()) {
            tc_skeleton_rebuild_roots(s);
        }
    }

    // Get by UUID from registry
    static TcSkeleton from_uuid(const std::string& uuid) {
        tc_skeleton_handle h = tc_skeleton_find(uuid.c_str());
        if (tc_skeleton_handle_is_invalid(h)) {
            return TcSkeleton();
        }
        return TcSkeleton(h);
    }

    // Get or create by UUID
    static TcSkeleton get_or_create(const std::string& uuid) {
        tc_skeleton_handle h = tc_skeleton_get_or_create(uuid.c_str());
        if (tc_skeleton_handle_is_invalid(h)) {
            return TcSkeleton();
        }
        return TcSkeleton(h);
    }

    // Create new skeleton
    static TcSkeleton create(const std::string& name = "", const std::string& uuid_hint = "") {
        const char* uuid = uuid_hint.empty() ? nullptr : uuid_hint.c_str();
        tc_skeleton_handle h = tc_skeleton_create(uuid);
        if (tc_skeleton_handle_is_invalid(h)) {
            return TcSkeleton();
        }

        tc_skeleton* s = tc_skeleton_get(h);
        if (s && !name.empty()) {
            s->header.name = tgfx_intern_string(name.c_str());
        }

        return TcSkeleton(h);
    }
};

} // namespace termin
