// test_mesh.c - Tests for mesh API
#include <stdio.h>
#include <string.h>
#include <tgfx/resources/tc_mesh.h>
#include <tgfx/resources/tc_mesh_registry.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s (line %d)\n", msg, __LINE__); \
            return 1; \
        } \
    } while(0)

static int test_vertex_layout(void) {
    printf("Testing Vertex Layout...\n");

    tc_vertex_layout layout;
    tc_vertex_layout_init(&layout);

    TEST_ASSERT(layout.stride == 0, "initial stride is 0");
    TEST_ASSERT(layout.attrib_count == 0, "initial attrib count is 0");

    tc_vertex_layout_add(&layout, "position", 3, TC_ATTRIB_FLOAT32, 0);
    TEST_ASSERT(layout.stride == 12, "stride is 12 after position");

    tc_vertex_layout_add(&layout, "normal", 3, TC_ATTRIB_FLOAT32, 1);
    TEST_ASSERT(layout.stride == 24, "stride is 24 after normal");

    tc_vertex_layout_add(&layout, "uv", 2, TC_ATTRIB_FLOAT32, 2);
    TEST_ASSERT(layout.stride == 32, "stride is 32 after uv");

    const tc_vertex_attrib* pos = tc_vertex_layout_find(&layout, "position");
    TEST_ASSERT(pos != NULL && pos->offset == 0, "position at offset 0");

    const tc_vertex_attrib* uv = tc_vertex_layout_find(&layout, "uv");
    TEST_ASSERT(uv != NULL && uv->offset == 24, "uv at offset 24");

    tc_vertex_layout mesh3 = tc_vertex_layout_pos_normal_uv();
    TEST_ASSERT(mesh3.stride == 32, "predefined mesh3 layout");

    printf("  Vertex Layout: PASS\n");
    return 0;
}

static int test_mesh_global_api(void) {
    printf("Testing Mesh Global API...\n");

    tc_mesh_init();
    TEST_ASSERT(tc_mesh_count() == 0, "initial count is 0");

    // Add mesh
    tc_mesh* mesh1 = tc_mesh_add("test-001");
    TEST_ASSERT(mesh1 != NULL, "add returns mesh");
    TEST_ASSERT(tc_mesh_count() == 1, "count is 1");
    TEST_ASSERT(strcmp(mesh1->header.uuid, "test-001") == 0, "uuid matches");

    // Get by UUID
    tc_mesh_handle h1 = tc_mesh_find("test-001");
    TEST_ASSERT(tc_mesh_get(h1) == mesh1, "get returns same mesh");
    TEST_ASSERT(tc_mesh_contains("test-001"), "contains");
    TEST_ASSERT(!tc_mesh_contains("nonexistent"), "not contains");

    // Duplicate rejected
    TEST_ASSERT(tc_mesh_add("test-001") == NULL, "duplicate rejected");

    // Auto UUID
    tc_mesh* mesh2 = tc_mesh_add(NULL);
    TEST_ASSERT(mesh2 != NULL, "auto uuid works");
    TEST_ASSERT(tc_mesh_count() == 2, "count is 2");

    // Remove
    TEST_ASSERT(tc_mesh_remove("test-001"), "remove returns true");
    TEST_ASSERT(tc_mesh_count() == 1, "count is 1");
    TEST_ASSERT(!tc_mesh_contains("test-001"), "removed mesh gone");

    tc_mesh_shutdown();

    printf("  Mesh Global API: PASS\n");
    return 0;
}

static int test_mesh_data(void) {
    printf("Testing Mesh Data...\n");

    tc_mesh_init();

    tc_mesh* mesh = tc_mesh_add("data-test");
    TEST_ASSERT(mesh->header.version == 1, "initial version");

    tc_vertex_layout layout = tc_vertex_layout_pos_normal_uv();
    float verts[] = {
        0, 0, 0,  0, 1, 0,  0, 0,
        1, 0, 0,  0, 1, 0,  1, 0,
        0, 0, 1,  0, 1, 0,  0, 1,
    };
    uint32_t idx[] = { 0, 1, 2 };

    tc_mesh_set_data(mesh, verts, 3, &layout, idx, 3, "data-test");

    TEST_ASSERT(mesh->vertex_count == 3, "vertex count");
    TEST_ASSERT(mesh->index_count == 3, "index count");
    TEST_ASSERT(mesh->header.version == 2, "version bumped");
    TEST_ASSERT(tc_mesh_triangle_count(mesh) == 1, "triangle count");

    tc_mesh_bump_version(mesh);
    TEST_ASSERT(mesh->header.version == 3, "manual bump");

    tc_mesh_shutdown();

    printf("  Mesh Data: PASS\n");
    return 0;
}

static int test_ref_counting(void) {
    printf("Testing Ref Counting...\n");

    tc_mesh_init();

    // Create mesh (ref_count starts at 0)
    tc_mesh_handle h1 = tc_mesh_get_or_create("ref-test");
    tc_mesh* mesh1 = tc_mesh_get(h1);
    TEST_ASSERT(mesh1 != NULL, "get_or_create returns mesh");
    TEST_ASSERT(mesh1->header.ref_count == 0, "initial ref_count is 0");
    TEST_ASSERT(tc_mesh_count() == 1, "count is 1");

    // get_or_create again returns same mesh (no auto-increment)
    tc_mesh_handle h2 = tc_mesh_get_or_create("ref-test");
    tc_mesh* mesh2 = tc_mesh_get(h2);
    TEST_ASSERT(mesh2 == mesh1, "same mesh returned");
    TEST_ASSERT(mesh1->header.ref_count == 0, "ref_count unchanged");
    TEST_ASSERT(tc_mesh_count() == 1, "count still 1");

    // add_ref increments
    tc_mesh_add_ref(mesh1);
    TEST_ASSERT(mesh1->header.ref_count == 1, "ref_count is 1");

    tc_mesh_add_ref(mesh1);
    TEST_ASSERT(mesh1->header.ref_count == 2, "ref_count is 2");

    // release decrements
    tc_mesh_release(mesh1);
    TEST_ASSERT(mesh1->header.ref_count == 1, "ref_count is 1");
    TEST_ASSERT(tc_mesh_count() == 1, "mesh still exists");

    // Last release destroys mesh
    tc_mesh_release(mesh1);
    TEST_ASSERT(tc_mesh_count() == 0, "mesh destroyed");
    TEST_ASSERT(!tc_mesh_contains("ref-test"), "mesh gone from registry");

    tc_mesh_shutdown();

    printf("  Ref Counting: PASS\n");
    return 0;
}

int main(void) {
    printf("=== Mesh Tests ===\n\n");

    int result = 0;
    result |= test_vertex_layout();
    result |= test_mesh_global_api();
    result |= test_mesh_data();
    result |= test_ref_counting();

    printf("\n");
    if (result == 0) {
        printf("=== ALL TESTS PASSED ===\n");
    } else {
        printf("=== SOME TESTS FAILED ===\n");
    }

    return result;
}
