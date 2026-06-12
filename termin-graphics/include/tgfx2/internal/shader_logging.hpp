#pragma once

#include <cstdlib>
#include <cstring>

namespace tgfx::internal {

inline bool shader_verbose_logging_enabled() {
    const char* env = std::getenv("TERMIN_SHADER_VERBOSE");
    if (!env || env[0] == '\0') {
        return false;
    }
    return std::strcmp(env, "1") == 0 ||
           std::strcmp(env, "true") == 0 ||
           std::strcmp(env, "TRUE") == 0 ||
           std::strcmp(env, "on") == 0 ||
           std::strcmp(env, "ON") == 0 ||
           std::strcmp(env, "yes") == 0 ||
           std::strcmp(env, "YES") == 0;
}

} // namespace tgfx::internal
