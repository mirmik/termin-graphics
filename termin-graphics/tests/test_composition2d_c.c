#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <math.h>

#include "tgfx2/composition2d.h"

static bool near(float left, float right) {
    return fabsf(left - right) <= 1e-5f;
}

int main(void) {
    tgfx2_composition_state2d root = tgfx2_composition_state2d_identity();
    tgfx2_composition_layer2d layer = tgfx2_composition_layer2d_identity();
    layer.transform = tc_affine2f_mul(tc_affine2f_translation(10.0f, -4.0f), tc_affine2f_scaling(2.0f, 3.0f));
    layer.opacity = 0.5f;

    tgfx2_composition_state2d state;
    assert(tgfx2_composition_state2d_push(&root, &layer, &state));
    assert(state.visible && state.invertible && near(state.opacity, 0.5f));

    tc_vec2f world;
    tc_vec2f local;
    assert(tgfx2_composition_state2d_map_point_to_world(&state, (tc_vec2f){2.0f, 5.0f}, &world));
    assert(tgfx2_composition_state2d_map_point_from_world(&state, world, &local));
    assert(near(local.x, 2.0f) && near(local.y, 5.0f));

    tc_bounds2f bounds;
    assert(tgfx2_composition_state2d_map_bounds_to_world(
        &state, (tc_bounds2f){0.0f, 0.0f, 4.0f, 6.0f}, &bounds));
    assert(near(bounds.x0, 10.0f) && near(bounds.y0, -4.0f));
    assert(near(bounds.x1, 18.0f) && near(bounds.y1, 14.0f));
}
