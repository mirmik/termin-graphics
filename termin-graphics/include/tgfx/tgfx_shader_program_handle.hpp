#pragma once

extern "C" {
#include <tgfx/resources/tc_shader_program_registry.h>
}

#include <string>

namespace termin {

class TcShaderProgram {
public:
    tc_shader_program_handle handle = tc_shader_program_handle_invalid();

    TcShaderProgram() = default;
    explicit TcShaderProgram(tc_shader_program_handle value) : handle(value) {
        tc_shader_program_retain(get());
    }
    TcShaderProgram(const TcShaderProgram& other) : handle(other.handle) {
        tc_shader_program_retain(get());
    }
    TcShaderProgram(TcShaderProgram&& other) noexcept : handle(other.handle) {
        other.handle = tc_shader_program_handle_invalid();
    }
    TcShaderProgram& operator=(const TcShaderProgram& other) {
        if (this == &other) return *this;
        tc_shader_program_release(get());
        handle = other.handle;
        tc_shader_program_retain(get());
        return *this;
    }
    TcShaderProgram& operator=(TcShaderProgram&& other) noexcept {
        if (this == &other) return *this;
        tc_shader_program_release(get());
        handle = other.handle;
        other.handle = tc_shader_program_handle_invalid();
        return *this;
    }
    ~TcShaderProgram() {
        tc_shader_program_release(get());
    }

    static TcShaderProgram declare(const std::string& uuid, const std::string& name) {
        return TcShaderProgram(tc_shader_program_declare(uuid.c_str(), name.c_str()));
    }
    static TcShaderProgram find(const std::string& uuid) {
        return TcShaderProgram(tc_shader_program_find(uuid.c_str()));
    }

    tc_shader_program* get() const { return tc_shader_program_get(handle); }
    bool is_valid() const { return tc_shader_program_is_valid(handle); }
    const char* uuid() const { return get() ? get()->header.uuid : ""; }
    const char* name() const { return get() && get()->header.name ? get()->header.name : ""; }
    uint32_t version() const { return tc_shader_program_version(get()); }
};

}  // namespace termin
