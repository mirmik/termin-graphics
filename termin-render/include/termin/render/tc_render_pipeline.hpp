#pragma once

extern "C" {
#include <render/tc_render_pipeline_registry.h>
}

#include <string>

namespace termin {

class TcRenderPipeline {
public:
    tc_render_pipeline_handle handle = tc_render_pipeline_handle_invalid();

    TcRenderPipeline() = default;
    explicit TcRenderPipeline(tc_render_pipeline_handle value) : handle(value) {
        tc_render_pipeline_retain(get());
    }
    TcRenderPipeline(const TcRenderPipeline& other) : handle(other.handle) {
        tc_render_pipeline_retain(get());
    }
    TcRenderPipeline(TcRenderPipeline&& other) noexcept : handle(other.handle) {
        other.handle = tc_render_pipeline_handle_invalid();
    }
    TcRenderPipeline& operator=(const TcRenderPipeline& other) {
        if (this == &other) return *this;
        tc_render_pipeline_release(get());
        handle = other.handle;
        tc_render_pipeline_retain(get());
        return *this;
    }
    TcRenderPipeline& operator=(TcRenderPipeline&& other) noexcept {
        if (this == &other) return *this;
        tc_render_pipeline_release(get());
        handle = other.handle;
        other.handle = tc_render_pipeline_handle_invalid();
        return *this;
    }
    ~TcRenderPipeline() { tc_render_pipeline_release(get()); }

    static TcRenderPipeline declare(const std::string& uuid, const std::string& name) {
        return TcRenderPipeline(tc_render_pipeline_declare(uuid.c_str(), name.c_str()));
    }
    static TcRenderPipeline find(const std::string& uuid) {
        return TcRenderPipeline(tc_render_pipeline_find(uuid.c_str()));
    }
    tc_render_pipeline* get() const { return tc_render_pipeline_get(handle); }
    bool is_valid() const { return tc_render_pipeline_is_valid(handle); }
    const char* uuid() const { return get() ? get()->header.uuid : ""; }
    const char* name() const { return get() && get()->header.name ? get()->header.name : ""; }
    uint32_t version() const { return tc_render_pipeline_version(get()); }
};

} // namespace termin
