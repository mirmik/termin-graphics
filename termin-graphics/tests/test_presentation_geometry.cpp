#include "tgfx2/presentation_geometry.hpp"

#include <cstdio>

bool expect_rect(uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh,
                 int x0, int y0, int x1, int y1) {
    const auto r = tgfx::aspect_fit_rect(sw, sh, dw, dh);
    if (r.x0 == x0 && r.y0 == y0 && r.x1 == x1 && r.y1 == y1) return true;
    std::fprintf(stderr, "%ux%u -> %ux%u: [%d,%d,%d,%d]\n",
                 sw, sh, dw, dh, r.x0, r.y0, r.x1, r.y1);
    return false;
}

int main() {
    return expect_rect(320, 240, 320, 240, 0, 0, 320, 240) &&
           expect_rect(160, 120, 320, 240, 0, 0, 320, 240) &&
           expect_rect(640, 480, 320, 240, 0, 0, 320, 240) &&
           expect_rect(320, 180, 320, 240, 0, 30, 320, 210) &&
           expect_rect(180, 320, 320, 240, 92, 0, 227, 240) &&
           expect_rect(0, 10, 320, 240, 0, 0, 0, 0) ? 0 : 1;
}
