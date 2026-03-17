#pragma once

#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _MSC_VER
#define tc_strdup _strdup
#else
#define tc_strdup strdup
#endif

extern "C" {
#include "core/tc_scene.h"
#include "render/tc_pass.h"
#include "tc_value.h"
}

namespace termin {

class CxxFramePass;
class TcPass;

class TcPassRef {
public:
    tc_pass* _c = nullptr;

    TcPassRef() = default;
    explicit TcPassRef(tc_pass* p) : _c(p) {}

    bool valid() const { return _c != nullptr; }

    std::string pass_name() const {
        return _c && _c->pass_name ? _c->pass_name : "";
    }

    void set_pass_name(const std::string& name) {
        if (_c) {
            tc_pass_set_name(_c, name.c_str());
        }
    }

    bool enabled() const { return _c ? _c->enabled : true; }
    void set_enabled(bool v) { if (_c) _c->enabled = v; }

    bool passthrough() const { return _c ? _c->passthrough : false; }
    void set_passthrough(bool v) { if (_c) _c->passthrough = v; }

    std::string type_name() const {
        return _c ? tc_pass_type_name(_c) : "BrokenPass_NullPtr";
    }

    bool is_inplace() const {
        return _c ? tc_pass_is_inplace(_c) : false;
    }

    std::string viewport_name() const {
        return _c && _c->viewport_name ? _c->viewport_name : "";
    }

    void set_viewport_name(const std::string& name) {
        if (!_c) return;
        if (_c->viewport_name) {
            free((void*)_c->viewport_name);
        }
        _c->viewport_name = name.empty() ? nullptr : tc_strdup(name.c_str());
    }

    void* object_ptr() const;
    bool set_field(const std::string& field_name, const tc_value& value);

    tc_pass* ptr() const { return _c; }
};

class TcPass {
public:
    tc_pass* _c = nullptr;

    TcPass() = default;
    explicit TcPass(tc_pass* p) : _c(p) {}

    ~TcPass() {
        if (_c) {
            tc_pass_free_external(_c);
            _c = nullptr;
        }
    }

    TcPass(const TcPass&) = delete;
    TcPass& operator=(const TcPass&) = delete;

    TcPass(TcPass&& other) noexcept : _c(other._c) {
        other._c = nullptr;
    }

    TcPass& operator=(TcPass&& other) noexcept {
        if (this != &other) {
            if (_c) tc_pass_free_external(_c);
            _c = other._c;
            other._c = nullptr;
        }
        return *this;
    }

    TcPassRef ref() const { return TcPassRef(_c); }

    std::string pass_name() const { return ref().pass_name(); }
    void set_pass_name(const std::string& name) { ref().set_pass_name(name); }

    bool enabled() const { return ref().enabled(); }
    void set_enabled(bool v) { ref().set_enabled(v); }

    bool passthrough() const { return ref().passthrough(); }
    void set_passthrough(bool v) { ref().set_passthrough(v); }

    std::string type_name() const { return ref().type_name(); }
    bool is_inplace() const { return ref().is_inplace(); }

    tc_pass* ptr() const { return _c; }
};

} // namespace termin
