#include "guard_c.h"

#include "render/tc_render_target.h"
#include "tgfx/resources/tc_texture.h"
#include "tgfx/resources/tc_texture_registry.h"

#include <limits.h>

GUARD_C_TEST(test_render_target_rejects_invalid_dimensions_without_mutation) {
    tc_render_target_handle target = tc_render_target_new("dimension-validation");
    GUARD_C_REQUIRE(tc_render_target_alive(target));

    tc_render_target_set_width(target, 64);
    tc_render_target_set_height(target, 48);
    tc_render_target_ensure_textures(target);

    tc_texture* color = tc_texture_get(tc_render_target_get_color_texture(target));
    tc_texture* depth = tc_texture_get(tc_render_target_get_depth_texture(target));
    GUARD_C_REQUIRE(color != NULL);
    GUARD_C_REQUIRE(depth != NULL);
    GUARD_C_CHECK_EQ_INT(64, tc_render_target_get_width(target));
    GUARD_C_CHECK_EQ_INT(48, tc_render_target_get_height(target));
    GUARD_C_CHECK_EQ_UINT(64, color->width);
    GUARD_C_CHECK_EQ_UINT(48, color->height);

    tc_render_target_set_width(target, -1);
    tc_render_target_set_height(target, 0);
    tc_render_target_set_width(target, INT_MAX);
    tc_render_target_set_height(target, TC_RENDER_TARGET_MAX_DIMENSION + 1);
    GUARD_C_CHECK_EQ_INT(64, tc_render_target_get_width(target));
    GUARD_C_CHECK_EQ_INT(48, tc_render_target_get_height(target));

    tc_render_target_set_width(target, 128);
    tc_render_target_set_height(target, 96);
    tc_render_target_ensure_textures(target);
    GUARD_C_CHECK_EQ_INT(128, tc_render_target_get_width(target));
    GUARD_C_CHECK_EQ_INT(96, tc_render_target_get_height(target));
    GUARD_C_CHECK_EQ_UINT(128, color->width);
    GUARD_C_CHECK_EQ_UINT(96, color->height);

    tc_render_target_free(target);
    return 0;
}

int main(int argc, char** argv) {
    GUARD_C_BEGIN_ARGS(argc, argv);
    GUARD_C_RUN(test_render_target_rejects_invalid_dimensions_without_mutation);
    return GUARD_C_END();
}
