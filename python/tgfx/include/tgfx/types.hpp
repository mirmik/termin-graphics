#pragma once

#include <cstdint>

namespace termin {

// RGBA color with float components [0, 1].
struct Color4 {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    Color4() = default;
    Color4(float r, float g, float b, float a = 1.0f) : r(r), g(g), b(b), a(a) {}

    static Color4 black() { return {0, 0, 0, 1}; }
    static Color4 white() { return {1, 1, 1, 1}; }
    static Color4 red() { return {1, 0, 0, 1}; }
    static Color4 green() { return {0, 1, 0, 1}; }
    static Color4 blue() { return {0, 0, 1, 1}; }
    static Color4 transparent() { return {0, 0, 0, 0}; }
};

// 2D size with integer components.
struct Size2i {
    int width = 0;
    int height = 0;

    Size2i() = default;
    Size2i(int w, int h) : width(w), height(h) {}

    bool operator==(const Size2i& other) const {
        return width == other.width && height == other.height;
    }
    bool operator!=(const Size2i& other) const {
        return !(*this == other);
    }
};

// 2D rectangle with integer coordinates.
struct Rect2i {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    Rect2i() = default;
    Rect2i(int x0, int y0, int x1, int y1) : x0(x0), y0(y0), x1(x1), y1(y1) {}

    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }

    static Rect2i from_size(int width, int height) {
        return {0, 0, width, height};
    }
    static Rect2i from_size(Size2i size) {
        return {0, 0, size.width, size.height};
    }
};

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
