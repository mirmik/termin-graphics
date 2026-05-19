// glsl_preprocessor.cpp - Global GLSL preprocessor instance
#include <termin/materials/glsl_preprocessor.hpp>

namespace termin {

// Single global instance (shared across all modules)
GlslPreprocessor& glsl_preprocessor() {
    static GlslPreprocessor instance;
    return instance;
}

} // namespace termin
