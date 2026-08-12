#pragma once

#include <cstdint>
#include <string>

#include <glad/glad.h>

#include "tgfx2/opengl/gl_features.hpp"
#include "tgfx2/tgfx2_api.h"

namespace tgfx {

    // Capability-gated boundary for GL operations which are not shared by
    // desktop OpenGL 3.3 and WebGL 2. Command/resource code must use this
    // object instead of calling optional GL entry points directly.
    class TGFX2_TYPE_API GlPlatformOperations {
    public:
        explicit GlPlatformOperations(const GlFeatureSet& features)
            : features_(features) {}

        static bool runtime_has_clip_control(uint32_t major, uint32_t minor);

        bool apply_clip_space_contract(std::string& error) const;
        bool apply_polygon_mode(GLenum mode) const;

        bool draw_elements(GLenum topology,
                           GLsizei index_count,
                           GLenum index_type,
                           const void* index_offset,
                           GLint base_vertex) const;
        bool draw_elements_instanced(GLenum topology,
                                     GLsizei index_count,
                                     GLenum index_type,
                                     const void* index_offset,
                                     GLsizei instance_count,
                                     GLint base_vertex) const;

        bool allocate_multisample_texture_2d(GLenum target,
                                             GLsizei samples,
                                             GLenum internal_format,
                                             GLsizei width,
                                             GLsizei height,
                                             bool fixed_sample_locations) const;

        bool issue_timestamp(GLuint query) const;
        bool read_timestamp(GLuint query, bool wait, uint64_t& timestamp, bool& available) const;

    private:
        GlFeatureSet features_;
    };

} // namespace tgfx
