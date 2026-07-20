#pragma once

extern "C" {
#include <render/tc_pipeline_template_registry.h>
}

#include <string>

namespace termin {

class TcPipelineTemplate {
public:
    tc_pipeline_template_handle handle = tc_pipeline_template_handle_invalid();

    TcPipelineTemplate() = default;
    explicit TcPipelineTemplate(tc_pipeline_template_handle value) : handle(value) {
        tc_pipeline_template_retain(get());
    }
    TcPipelineTemplate(const TcPipelineTemplate& other) : handle(other.handle) {
        tc_pipeline_template_retain(get());
    }
    TcPipelineTemplate(TcPipelineTemplate&& other) noexcept : handle(other.handle) {
        other.handle = tc_pipeline_template_handle_invalid();
    }
    TcPipelineTemplate& operator=(const TcPipelineTemplate& other) {
        if (this == &other) return *this;
        tc_pipeline_template_release(get());
        handle = other.handle;
        tc_pipeline_template_retain(get());
        return *this;
    }
    TcPipelineTemplate& operator=(TcPipelineTemplate&& other) noexcept {
        if (this == &other) return *this;
        tc_pipeline_template_release(get());
        handle = other.handle;
        other.handle = tc_pipeline_template_handle_invalid();
        return *this;
    }
    ~TcPipelineTemplate() { tc_pipeline_template_release(get()); }

    static TcPipelineTemplate declare(const std::string& uuid, const std::string& name) {
        return TcPipelineTemplate(tc_pipeline_template_declare(uuid.c_str(), name.c_str()));
    }
    static TcPipelineTemplate find(const std::string& uuid) {
        return TcPipelineTemplate(tc_pipeline_template_find(uuid.c_str()));
    }
    tc_pipeline_template* get() const { return tc_pipeline_template_get(handle); }
    bool is_valid() const { return tc_pipeline_template_is_valid(handle); }
    const char* uuid() const { return get() ? get()->header.uuid : ""; }
    const char* name() const { return get() && get()->header.name ? get()->header.name : ""; }
    uint32_t version() const { return tc_pipeline_template_version(get()); }
};

} // namespace termin
