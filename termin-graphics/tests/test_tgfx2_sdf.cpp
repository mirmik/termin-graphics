// SDF font atlas smoke test — exercises FontAtlas SDF path without GPU.
// Run with: ./tgfx2_sdf_test
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "tgfx2/descriptors.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/font_atlas.hpp"
#include "tgfx2/i_render_device.hpp"
#include "tgfx2/opengl/opengl_render_device.hpp"
#include "tgfx2/render_context.hpp"

int main() {
    if (!glfwInit()) {
        fprintf(stderr, "GLFW init failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(800, 200, "SDF test", nullptr, nullptr);
    if (!window) {
        fprintf(stderr, "Window creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "GLAD init failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Create device + context.
    auto device = tgfx::create_device(tgfx::BackendType::OpenGL);
    if (!device) {
        fprintf(stderr, "create_device failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    tgfx::RenderContext2 ctx(*device, 800, 200);

    // Load font.
    const char* font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    tgfx::FontAtlas font(font_path, 14);
    printf("Font loaded: %s\n", font_path);
    printf("SDF config: enabled=%d threshold=%d ref=%d spread=%d\n",
           font.sdf_enabled(), font.sdf_threshold_px(),
           font.sdf_reference_px(), font.sdf_spread());

    // Test string — covers ASCII printable, some CJK, symbols.
    const char* test_str =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789 "
        "!@#$%^&*() []{}|;:',.<>?/`~"
        "\xe2\x96\xb6\xe2\x96\xbc\xe2\x86\x90\xe2\x86\x92";  // ▶▼←→

    // Exercise bitmap path (below threshold).
    printf("\n--- Bitmap path ---\n");
    for (float sz : {8.0f, 10.0f, 12.0f, 14.0f, 16.0f}) {
        font.ensure_glyphs(test_str, sz, &ctx);
        auto m = font.measure_text(test_str, sz);
        printf("  size=%.0f  measure=(%.1f, %.1f)  sdf=%d\n",
               sz, m.width, m.height, font.is_sdf_size(sz));
    }
    printf("  Bitmap atlas texture: id=%u\n", font.ensure_texture(&ctx).id);

    // Exercise SDF path (above threshold).
    printf("\n--- SDF path ---\n");
    for (float sz : {18.0f, 20.0f, 24.0f, 28.0f, 32.0f, 48.0f, 64.0f}) {
        font.ensure_glyphs(test_str, sz, &ctx);
        auto m = font.measure_text(test_str, sz);
        printf("  size=%.0f  measure=(%.1f, %.1f)  sdf=%d\n",
               sz, m.width, m.height, font.is_sdf_size(sz));
    }
    printf("  SDF atlas texture: id=%u\n", font.sdf_atlas_texture(&ctx).id);

    // Verify individual glyph lookup works for SDF.
    printf("\n--- Glyph lookup ---\n");
    uint32_t test_cps[] = {'A', 'a', '0', '@', 'g', 'W', 0};
    for (int i = 0; test_cps[i]; ++i) {
        for (float sz : {14.0f, 24.0f}) {
            const auto* gi = font.get_glyph(test_cps[i], sz);
            if (gi) {
                printf("  U+%04X size=%.0f sdf=%d uv=(%.3f,%.3f)-(%.3f,%.3f) "
                       "size=(%.1f,%.1f) adv=%.1f\n",
                       test_cps[i], sz, font.is_sdf_size(sz),
                       gi->u0, gi->v0, gi->u1, gi->v1,
                       gi->width_px, gi->height_px, gi->advance_px);
            } else {
                printf("  U+%04X size=%.0f — MISSING\n", test_cps[i], sz);
            }
        }
    }

    // Preload stress: many codepoints at SDF size.
    printf("\n--- SDF preload stress ---\n");
    const char* stress_str =
        " !\"#$%&'()*+,-./0123456789:;<=>?@"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
        "abcdefghijklmnopqrstuvwxyz{|}~"
        "\xe2\x96\xb6\xe2\x96\xbc\xe2\x96\xb2\xe2\x97\x80"  // ▶▼▲◀
        "\xe2\x96\xa0\xe2\x96\xa1\xe2\x96\xa3\xe2\x96\xab"  // ■□▣▫
        "\xe2\x97\x8b\xe2\x97\x8f\xe2\x97\x88\xe2\x97\x86"  // ○●◈◆
        "\xe2\x80\xa2\xe2\x80\xa3\xe2\x97\xa6"              // •‣◦
        "\xe2\x9c\x93\xe2\x9c\x97\xe2\x9c\x95"              // ✓✗✕
        "\xe2\x86\x90\xe2\x86\x92\xe2\x86\x91\xe2\x86\x93";  // ←→↑↓
    font.ensure_glyphs(stress_str, 24.0f, &ctx);
    auto m = font.measure_text(stress_str, 24.0f);
    printf("  Stress string measure at 24px: %.1f x %.1f\n", m.width, m.height);

    // Cleanup.
    font.release_gpu();
    printf("\nTest completed successfully.\n");
    return 0;
}
