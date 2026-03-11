// tc_scene_skybox.c - Scene skybox implementation
#include "core/tc_scene_skybox.h"
#include "core/tc_scene.h"
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/resources/tc_mesh_registry.h>
#include <tgfx/resources/tc_material.h>
#include <tgfx/resources/tc_material_registry.h>
#include <tgfx/resources/tc_shader.h>
#include <tgfx/resources/tc_shader_registry.h>
#include <tgfx/resources/tc_resource.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Skybox Shaders
// ============================================================================

static const char* SKYBOX_VERTEX_SHADER =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_position;\n"
    "\n"
    "uniform mat4 u_view;\n"
    "uniform mat4 u_projection;\n"
    "\n"
    "out vec3 v_dir;\n"
    "\n"
    "void main() {\n"
    "    mat4 view_no_translation = mat4(mat3(u_view));\n"
    "    v_dir = a_position;\n"
    "    gl_Position = u_projection * view_no_translation * vec4(a_position, 1.0);\n"
    "}\n";

static const char* SKYBOX_GRADIENT_FRAGMENT_SHADER =
    "#version 330 core\n"
    "\n"
    "in vec3 v_dir;\n"
    "out vec4 FragColor;\n"
    "\n"
    "uniform vec3 u_skybox_top_color;\n"
    "uniform vec3 u_skybox_bottom_color;\n"
    "\n"
    "void main() {\n"
    "    float t = normalize(v_dir).z * 0.5 + 0.5;\n"
    "    FragColor = vec4(mix(u_skybox_bottom_color, u_skybox_top_color, t), 1.0);\n"
    "}\n";

static const char* SKYBOX_SOLID_FRAGMENT_SHADER =
    "#version 330 core\n"
    "\n"
    "in vec3 v_dir;\n"
    "out vec4 FragColor;\n"
    "\n"
    "uniform vec3 u_skybox_color;\n"
    "\n"
    "void main() {\n"
    "    FragColor = vec4(u_skybox_color, 1.0);\n"
    "}\n";

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

// Create skybox cube mesh
static tc_mesh* create_skybox_cube_mesh(void) {
    // Try to find existing skybox mesh
    tc_mesh_handle h = tc_mesh_find("__builtin_skybox_cube");
    if (tc_mesh_is_valid(h)) {
        tc_mesh* mesh = tc_mesh_get(h);
        if (mesh) {
            tc_mesh_add_ref(mesh);
            return mesh;
        }
    }

    // Create new mesh
    h = tc_mesh_create("__builtin_skybox_cube");
    if (!tc_mesh_is_valid(h)) {
        return NULL;
    }

    tc_mesh* mesh = tc_mesh_get(h);
    if (!mesh) {
        return NULL;
    }

    // Setup position-only layout
    mesh->layout = tc_vertex_layout_pos();

    // Allocate and copy vertices
    size_t vertex_size = 8 * mesh->layout.stride;
    mesh->vertices = malloc(vertex_size);
    if (!mesh->vertices) {
        tc_mesh_destroy(h);
        return NULL;
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
        return NULL;
    }
    memcpy(mesh->indices, SKYBOX_INDICES, index_size);
    mesh->index_count = 36;

    mesh->draw_mode = TC_DRAW_TRIANGLES;

    // Mark as loaded
    mesh->header.version = 1;

    // Add ref for the caller
    tc_mesh_add_ref(mesh);

    return mesh;
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
    skybox->mesh = NULL;
    skybox->material = NULL;
    skybox->gradient_material = NULL;
    skybox->solid_material = NULL;
}

void tc_scene_skybox_free(tc_scene_skybox* skybox) {
    if (!skybox) return;
    if (skybox->mesh) {
        tc_mesh_release(skybox->mesh);
        skybox->mesh = NULL;
    }
    // material is an alias, don't release separately
    skybox->material = NULL;
    if (skybox->gradient_material) {
        tc_material_release(skybox->gradient_material);
        skybox->gradient_material = NULL;
    }
    if (skybox->solid_material) {
        tc_material_release(skybox->solid_material);
        skybox->solid_material = NULL;
    }
}

tc_mesh* tc_scene_skybox_ensure_mesh(tc_scene_skybox* skybox) {
    if (!skybox) return NULL;
    if (skybox->mesh) return skybox->mesh;

    // Create skybox mesh lazily
    skybox->mesh = create_skybox_cube_mesh();
    return skybox->mesh;
}

// Create skybox material with given shader
static tc_material* create_skybox_material(const char* name, const char* frag_source) {
    // Try to find existing material
    tc_material_handle mh = tc_material_find(name);
    if (tc_material_is_valid(mh)) {
        tc_material* mat = tc_material_get(mh);
        if (mat) {
            tc_material_add_ref(mat);
            return mat;
        }
    }

    // Create shader
    tc_shader_handle sh = tc_shader_from_sources(
        SKYBOX_VERTEX_SHADER,
        frag_source,
        NULL,  // no geometry shader
        name,
        NULL,  // no source path
        NULL   // auto-generate uuid
    );
    if (!tc_shader_is_valid(sh)) {
        return NULL;
    }

    // Add ref for material's usage + extra ref to keep builtin shader alive forever
    tc_shader* shader = tc_shader_get(sh);
    if (shader) {
        tc_shader_add_ref(shader);  // ref for material
        tc_shader_add_ref(shader);  // ref to prevent deletion (builtin shader)
    }

    // Create material
    mh = tc_material_create(NULL, name);
    if (!tc_material_is_valid(mh)) {
        return NULL;
    }

    tc_material* mat = tc_material_get(mh);
    if (!mat) {
        return NULL;
    }

    // Setup default phase with shader
    mat->phase_count = 1;
    mat->phases[0].shader = sh;
    mat->phases[0].state = tc_render_state_opaque();
    mat->phases[0].state.depth_write = 0;  // Skybox doesn't write depth
    mat->phases[0].state.cull = 0;         // Render inside of cube
    strncpy(mat->phases[0].phase_mark, "skybox", TC_PHASE_MARK_MAX - 1);

    tc_material_add_ref(mat);
    return mat;
}

tc_material* tc_scene_skybox_ensure_material(tc_scene_skybox* skybox, int type) {
    if (!skybox) return NULL;

    if (type == TC_SKYBOX_NONE) {
        skybox->material = NULL;
        return NULL;
    }

    if (type == TC_SKYBOX_SOLID) {
        if (!skybox->solid_material) {
            skybox->solid_material = create_skybox_material(
                "__builtin_skybox_solid",
                SKYBOX_SOLID_FRAGMENT_SHADER
            );
        }
        skybox->material = skybox->solid_material;
        return skybox->solid_material;
    }

    // Default: gradient
    if (!skybox->gradient_material) {
        skybox->gradient_material = create_skybox_material(
            "__builtin_skybox_gradient",
            SKYBOX_GRADIENT_FRAGMENT_SHADER
        );
    }
    skybox->material = skybox->gradient_material;
    return skybox->gradient_material;
}
