// Non-owning value-level 2D composition state shared by retained producers.
#ifndef TGFX2_COMPOSITION2D_H
#define TGFX2_COMPOSITION2D_H

#include <stdbool.h>

#include <geom/tc_affine2.h>
#include <tcbase/tc_types.h>

#include "tgfx2/tgfx2_api.h"

#ifdef __cplusplus
extern "C" {
#endif

// A local semantic layer. Composition order is parent * local: local points
// are transformed first, then the already accumulated parent placement.
typedef struct tgfx2_composition_layer2d {
    tc_affine2f transform;
    float opacity;
    bool visible;
} tgfx2_composition_layer2d;

// Fully evaluated placement. This is a plain borrowed-by-value result: it
// contains no topology, handles, callbacks or owned resources.
typedef struct tgfx2_composition_state2d {
    tc_affine2f local_to_world;
    tc_affine2f world_to_local;
    float opacity;
    bool visible;
    bool invertible;
} tgfx2_composition_state2d;

TGFX2_API tgfx2_composition_layer2d tgfx2_composition_layer2d_identity(void);
TGFX2_API tgfx2_composition_state2d tgfx2_composition_state2d_identity(void);

// Transactional: on failure, out_state is left unchanged and an error is
// logged. Singular transforms are valid placements but produce
// invertible=false. Non-finite transforms and opacity outside [0, 1] fail.
TGFX2_API bool tgfx2_composition_state2d_push(const tgfx2_composition_state2d* parent,
                                              const tgfx2_composition_layer2d* local,
                                              tgfx2_composition_state2d* out_state);

TGFX2_API bool tgfx2_composition_state2d_map_point_to_world(const tgfx2_composition_state2d* state,
                                                            tc_vec2f local_point,
                                                            tc_vec2f* out_world_point);
TGFX2_API bool tgfx2_composition_state2d_map_point_from_world(const tgfx2_composition_state2d* state,
                                                              tc_vec2f world_point,
                                                              tc_vec2f* out_local_point);
TGFX2_API bool tgfx2_composition_state2d_map_bounds_to_world(const tgfx2_composition_state2d* state,
                                                             tc_bounds2f local_bounds,
                                                             tc_bounds2f* out_world_bounds);

#ifdef __cplusplus
}
#endif

#endif // TGFX2_COMPOSITION2D_H
