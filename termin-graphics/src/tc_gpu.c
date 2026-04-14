// tc_gpu.c - Material GPU operations.
//
// Stage 8.2: the per-uniform glUniform* dispatch path
// (tc_material_phase_apply_uniforms and friends) has been removed.
// Materials now go through the std140 UBO dispatcher in
// material_ubo_apply.{hpp,cpp} (render_lib / termin-app), driven
// from tgfx2-migrated passes via ctx2->bind_uniform_buffer.
//
// This translation unit remains only so that tc_gpu.c keeps existing
// on disk while downstream CMake files still reference it. Once the
// modules.conf rebuild cycle lets us drop the source list entry, this
// file and its header can be removed entirely.

#include <tgfx/tc_gpu.h>
