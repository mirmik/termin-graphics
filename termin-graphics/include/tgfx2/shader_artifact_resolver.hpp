#pragma once

#include <cstdint>
#include <string>

#include "tgfx2/tgfx2_api.h"

namespace termin {

class TGFX2_TYPE_API ShaderArtifactResolver {
public:
    ShaderArtifactResolver() = default;
    ShaderArtifactResolver(
        std::string artifact_root,
        std::string cache_root,
        std::string compiler_path,
        bool dev_compile_enabled,
        bool environment_fallback = false
    );

    const std::string& artifact_root() const;
    const std::string& cache_root() const;
    const std::string& compiler_path() const;
    bool dev_compile_enabled() const;
    uint64_t revision() const { return revision_; }

    void configure(
        std::string artifact_root,
        std::string cache_root,
        std::string compiler_path,
        bool dev_compile_enabled
    );
    void set_artifact_root(std::string value);
    void set_cache_root(std::string value);
    void set_compiler_path(std::string value);
    void set_dev_compile_enabled(bool value);

private:
    std::string artifact_root_;
    std::string cache_root_;
    std::string compiler_path_;
    bool dev_compile_enabled_ = false;
    bool environment_fallback_ = false;
    uint64_t revision_ = 1;
    mutable std::string environment_artifact_root_;
    mutable std::string environment_cache_root_;
    mutable std::string environment_compiler_path_;
};

// Compatibility resolver for legacy standalone tgfx users. Engine/runtime
// composition roots should configure their own resolver instead.
TGFX2_API ShaderArtifactResolver& tgfx2_legacy_shader_artifact_resolver();

} // namespace termin
