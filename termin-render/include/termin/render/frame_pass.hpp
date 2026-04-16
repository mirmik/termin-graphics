#pragma once

#include <algorithm>
#include <any>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _MSC_VER
#define tc_strdup _strdup
#else
#define tc_strdup strdup
#endif

extern "C" {
#include "render/tc_pass.h"
}

#include "tc_inspect_cpp.hpp"

#include "tgfx/handles.hpp"
#include <termin/render/resource_spec.hpp>
#include <termin/render/render_export.hpp>

namespace termin {

struct ExecuteContext;
class FrameGraphCapture;
class GraphicsBackend;

struct InternalSymbolTiming {
    std::string name;
    double cpu_time_ms = 0.0;
    double gpu_time_ms = 0.0;
};

struct Rect4i {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

using ResourceMap = std::unordered_map<std::string, FrameGraphResource*>;
using FBOMap = ResourceMap;

class RENDER_API CxxFramePass {
public:
    tc_pass _c;

private:
    std::atomic<int> _ref_count{0};
    mutable std::vector<std::string> _cached_aliases;
    mutable std::vector<std::string> _cached_symbols;
    static const tc_pass_vtable _cpp_vtable;

    static void _cb_execute(tc_pass* p, void* ctx);
    static size_t _cb_get_reads(tc_pass* p, const char** out, size_t max);
    static size_t _cb_get_writes(tc_pass* p, const char** out, size_t max);
    static size_t _cb_get_inplace_aliases(tc_pass* p, const char** out, size_t max);
    static size_t _cb_get_resource_specs(tc_pass* p, void* out, size_t max);
    static size_t _cb_get_internal_symbols(tc_pass* p, const char** out, size_t max);
    static void _cb_destroy(tc_pass* p);

public:
    CxxFramePass();
    virtual ~CxxFramePass();

    CxxFramePass(const CxxFramePass&) = delete;
    CxxFramePass& operator=(const CxxFramePass&) = delete;

    tc_pass* tc_pass_ptr() { return &_c; }
    const tc_pass* tc_pass_ptr() const { return &_c; }

    static CxxFramePass* from_tc(tc_pass* p) {
        if (!p || p->kind != TC_NATIVE_PASS) return nullptr;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
        return reinterpret_cast<CxxFramePass*>(reinterpret_cast<char*>(p) - offsetof(CxxFramePass, _c));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    }

    static const CxxFramePass* from_tc(const tc_pass* p) {
        if (!p || p->kind != TC_NATIVE_PASS) return nullptr;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
        return reinterpret_cast<const CxxFramePass*>(reinterpret_cast<const char*>(p) - offsetof(CxxFramePass, _c));
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    }

    void link_to_type_registry(const char* type_name) {
        if (!type_name) return;
        if (!tc_pass_registry_has(type_name)) {
            tc_pass_registry_register(type_name, nullptr, nullptr, TC_NATIVE_PASS);
        }
        tc_type_entry* entry = tc_pass_registry_get_entry(type_name);
        if (entry) {
            _c.type_entry = entry;
            _c.type_version = entry->version;
        }
    }

    void set_owner_ref(void* owner, const tc_pass_ref_vtable* ref_vt) {
        _c.body = owner;
        if (ref_vt) _c.ref_vtable = ref_vt;
    }

    void set_python_ref(void* body, const tc_pass_ref_vtable* ref_vt) {
        set_owner_ref(body, ref_vt);
    }

    std::string pass_name_get() const { return _c.pass_name ? _c.pass_name : ""; }
    void pass_name_set(const std::string& name) { tc_pass_set_name(&_c, name.c_str()); }

    bool enabled_get() const { return _c.enabled; }
    void enabled_set(bool v) { _c.enabled = v; }

    std::string viewport_name_get() const {
        return _c.viewport_name ? _c.viewport_name : "";
    }

    void viewport_name_set(const std::string& name) {
        if (_c.viewport_name) free(_c.viewport_name);
        _c.viewport_name = name.empty() ? nullptr : tc_strdup(name.c_str());
    }

    std::string debug_internal_symbol_get() const {
        return _c.debug_internal_symbol ? _c.debug_internal_symbol : "";
    }

    void debug_internal_symbol_set(const std::string& sym) {
        if (_c.debug_internal_symbol) free(_c.debug_internal_symbol);
        _c.debug_internal_symbol = sym.empty() ? nullptr : tc_strdup(sym.c_str());
    }

    FrameGraphCapture* debug_capture_get() const {
        return reinterpret_cast<FrameGraphCapture*>(_c.debug_capture);
    }

    FrameGraphCapture* debug_capture() const {
        return debug_capture_get();
    }

    void debug_capture_set(FrameGraphCapture* capture) {
        _c.debug_capture = reinterpret_cast<void*>(capture);
    }

    void set_debug_capture(FrameGraphCapture* capture) {
        debug_capture_set(capture);
    }

    void clear_debug_capture() {
        _c.debug_capture = nullptr;
    }

    const std::string get_pass_name() const { return pass_name_get(); }
    void set_pass_name(const std::string& name) { pass_name_set(name); }
    bool get_enabled() const { return enabled_get(); }
    void set_enabled(bool v) { enabled_set(v); }
    const std::string get_viewport_name() const { return viewport_name_get(); }
    void set_viewport_name(const std::string& name) { viewport_name_set(name); }
    const std::string get_debug_internal_symbol() const { return debug_internal_symbol_get(); }
    void set_debug_internal_symbol(const std::string& sym) { debug_internal_symbol_set(sym); }
    const std::string get_debug_internal_point() const { return debug_internal_symbol_get(); }
    void set_debug_internal_point(const std::string& sym) { debug_internal_symbol_set(sym); }

    virtual void execute(ExecuteContext& ctx) {
        (void)ctx;
    }

    virtual std::set<const char*> compute_reads() const {
        return {};
    }

    virtual std::set<const char*> compute_writes() const {
        return {};
    }

    virtual std::vector<std::pair<std::string, std::string>> get_inplace_aliases() const {
        return {};
    }

    virtual std::vector<ResourceSpec> get_resource_specs() const {
        return {};
    }

    virtual std::vector<std::string> get_internal_symbols() const {
        return {};
    }

    virtual std::vector<InternalSymbolTiming> get_internal_symbols_with_timing() const {
        return {};
    }

    virtual void destroy() {}

    bool is_inplace() const {
        return !get_inplace_aliases().empty();
    }

    void clear_debug_internal_point() {
        debug_internal_symbol_set("");
    }

    std::set<const char*> required_resources() const {
        auto result = compute_reads();
        auto writes = compute_writes();
        result.insert(writes.begin(), writes.end());
        return result;
    }

    void retain() { _ref_count.fetch_add(1, std::memory_order_relaxed); }

    void release() {
        int old = _ref_count.fetch_sub(1, std::memory_order_acq_rel);
        if (old <= 1) {
            delete this;
        }
    }

    int ref_count() const {
        return _ref_count.load(std::memory_order_relaxed);
    }

private:
    void _init_tc_pass();
    void _cleanup_tc_pass();
};

#define TC_REGISTER_FRAME_PASS(PassClass)                                    \
    static tc_pass* _factory_##PassClass(void* /*userdata*/) {               \
        auto* pass = new PassClass();                                        \
        pass->retain();                                                      \
        tc_pass* c = pass->tc_pass_ptr();                                    \
        tc_type_entry* entry = tc_pass_registry_get_entry(#PassClass);       \
        if (entry) {                                                         \
            c->type_entry = entry;                                           \
            c->type_version = entry->version;                                \
        }                                                                    \
        return c;                                                            \
    }                                                                        \
    static struct _reg_##PassClass {                                         \
        _reg_##PassClass() {                                                 \
            tc_pass_registry_register(                                       \
                #PassClass, _factory_##PassClass, nullptr, TC_NATIVE_PASS);  \
        }                                                                    \
    } _reg_instance_##PassClass

#define TC_REGISTER_FRAME_PASS_DERIVED(PassClass, ParentClass)               \
    static tc_pass* _factory_##PassClass(void* /*userdata*/) {               \
        auto* pass = new PassClass();                                        \
        pass->retain();                                                      \
        tc_pass* c = pass->tc_pass_ptr();                                    \
        tc_type_entry* entry = tc_pass_registry_get_entry(#PassClass);       \
        if (entry) {                                                         \
            c->type_entry = entry;                                           \
            c->type_version = entry->version;                                \
        }                                                                    \
        return c;                                                            \
    }                                                                        \
    static struct _reg_##PassClass {                                         \
        _reg_##PassClass() {                                                 \
            tc_pass_registry_register(                                       \
                #PassClass, _factory_##PassClass, nullptr, TC_NATIVE_PASS);  \
            tc::InspectRegistry::instance().set_type_parent(                 \
                #PassClass, #ParentClass);                                   \
        }                                                                    \
    } _reg_instance_##PassClass

} // namespace termin
