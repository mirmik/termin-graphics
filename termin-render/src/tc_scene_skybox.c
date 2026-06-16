// tc_scene_skybox.c - Scene skybox implementation
#include "core/tc_scene_skybox.h"
#include "core/tc_scene.h"
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/resources/tc_mesh_registry.h>
#include <tgfx/resources/tc_material.h>
#include <tgfx/resources/tc_material_registry.h>
#include <tcbase/tc_resource.h>
#include <stdlib.h>
#include <string.h>

// Skybox cube geometry - 8 vertices, 12 triangles
static const float SKYBOX_VERTICES[8 * 3] = {
    -1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,
     1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,
    -1.0f,  1.0f,  1.0f,
};

static const uint32_t SKYBOX_INDICES[12 * 3] = {
    0, 1, 2,  0, 2, 3,  // back
    4, 6, 5,  4, 7, 6,  // front
    0, 4, 5,  0, 5, 1,  // bottom
    3, 2, 6,  3, 6, 7,  // top
    1, 5, 6,  1, 6, 2,  // right
    0, 3, 7,  0, 7, 4,  // left
};

static void release_mesh_handle(tc_mesh_handle* handle) {
    if (!handle || tc_mesh_handle_is_invalid(*handle)) return;
    tc_mesh* mesh = tc_mesh_get(*handle);
    if (mesh) {
        tc_mesh_release(mesh);
    }
    *handle = tc_mesh_handle_invalid();
}

static void release_material_handle(tc_material_handle* handle) {
    if (!handle || tc_material_handle_is_invalid(*handle)) return;
    tc_material* material = tc_material_get(*handle);
    if (material) {
        tc_material_release(material);
    }
    *handle = tc_material_handle_invalid();
}

// Create skybox cube mesh
static tc_mesh_handle create_skybox_cube_mesh(void) {
    // Try to find existing skybox mesh
    tc_mesh_handle h = tc_mesh_find("__builtin_skybox_cube");
    if (tc_mesh_is_valid(h)) {
        tc_mesh* mesh = tc_mesh_get(h);
        if (mesh) {
            tc_mesh_add_ref(mesh);
            return h;
        }
    }

    // Create new mesh
    h = tc_mesh_create("__builtin_skybox_cube");
    if (!tc_mesh_is_valid(h)) {
        return tc_mesh_handle_invalid();
    }

    tc_mesh* mesh = tc_mesh_get(h);
    if (!mesh) {
        return tc_mesh_handle_invalid();
    }

    // Setup position-only layout
    mesh->layout = tc_vertex_layout_pos();

    // Allocate and copy vertices
    size_t vertex_size = 8 * mesh->layout.stride;
    mesh->vertices = malloc(vertex_size);
    if (!mesh->vertices) {
        tc_mesh_destroy(h);
        return tc_mesh_handle_invalid();
    }
    memcpy(mesh->vertices, SKYBOX_VERTICES, vertex_size);
    mesh->vertex_count = 8;

    // Allocate and copy indices
    size_t index_size = 36 * sizeof(uint32_t);
    mesh->indices = (uint32_t*)malloc(index_size);
    if (!mesh->indices) {
        free(mesh->vertices);
        mesh->vertices = NULL;
        tc_mesh_destroy(h);
        return tc_mesh_handle_invalid();
    }
    memcpy(mesh->indices, SKYBOX_INDICES, index_size);
    mesh->index_count = 36;

    mesh->draw_mode = TC_DRAW_TRIANGLES;

    // Mark as loaded
    mesh->header.version = 1;

    // Add ref for the caller
    tc_mesh_add_ref(mesh);

    return h;
}

void tc_scene_skybox_init(tc_scene_skybox* skybox) {
    if (!skybox) return;
    skybox->type = TC_SKYBOX_GRADIENT;
    // Solid color: blue-ish default
    skybox->color[0] = 0.5f;
    skybox->color[1] = 0.7f;
    skybox->color[2] = 0.9f;
    // Gradient top: sky blue
    skybox->top_color[0] = 0.4f;
    skybox->top_color[1] = 0.6f;
    skybox->top_color[2] = 0.9f;
    // Gradient bottom: warm horizon
    skybox->bottom_color[0] = 0.6f;
    skybox->bottom_color[1] = 0.5f;
    skybox->bottom_color[2] = 0.4f;
    skybox->mesh = tc_mesh_handle_invalid();
    skybox->material = tc_material_handle_invalid();
}

void tc_scene_skybox_free(tc_scene_skybox* skybox) {
    if (!skybox) return;
    release_mesh_handle(&skybox->mesh);
    release_material_handle(&skybox->material);
}

tc_mesh* tc_scene_skybox_ensure_mesh(tc_scene_skybox* skybox) {
    if (!skybox) return NULL;
    if (!tc_mesh_handle_is_invalid(skybox->mesh)) {
        return tc_mesh_get(skybox->mesh);
    }

    // Create skybox mesh lazily
    skybox->mesh = create_skybox_cube_mesh();
    return tc_mesh_get(skybox->mesh);
}
