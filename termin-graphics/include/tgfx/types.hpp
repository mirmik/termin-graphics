#pragma once

#include <termin/geom/color.hpp>
#include <termin/geom/bounds2.hpp>
#include <termin/geom/size2.hpp>

namespace termin {

// Texture filter mode for FBO color attachments
enum class TextureFilter {
    LINEAR,
    NEAREST
};

// Draw mode for mesh rendering
enum class DrawMode {
    Triangles,
    Lines
};

} // namespace termin
